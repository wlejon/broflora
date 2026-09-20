(function () {
    'use strict';

    const accessor = (obj, name, get, set) =>
        Object.defineProperty(obj, name, { get, set, enumerable: true, configurable: true });
    const fn = (obj, name, value) =>
        Object.defineProperty(obj, name, { value, writable: true, enumerable: true, configurable: true });
    const mount = (root, name) => root[name] !== undefined ? root[name] : (root[name] = {});

    // ---- bro.flora -----------------------------------------------------------
    const broObj = mount(globalThis, "bro");
    const ns_flora = mount(broObj, "flora");
    accessor(ns_flora, "available", function () { return true; }, undefined);

    fn(ns_flora, "createWorld", function createWorld(opts) {
        var w = __bro_flora_native.createWorld(opts);
        if (w && Object.getPrototypeOf(w) !== FloraWorld.prototype) {
            Object.setPrototypeOf(w, FloraWorld.prototype);
        }
        return w;
    });

    fn(ns_flora, "leafCluster", function leafCluster(phyl, opts) {
        return __bro_flora_native.leafCluster(phyl, opts);
    });

    var _windState = { strength: 0, dirX: 0, dirY: 0 };
    var _densityState = 1.0;
    var _windTime = 0.0;
    var _activeBatches = [];
    var _activePlacements = [];
    var _nextBatchId = 0;

    function _createBatch(cfg) {
        if (!cfg) return null;
        var id = cfg.id !== undefined ? cfg.id : ('flora_batch_' + (++_nextBatchId));
        var mesh = cfg.mesh !== undefined ? cfg.mesh : null;
        var material = cfg.material !== undefined ? cfg.material : null;
        var windFactor = typeof cfg.windFactor === 'number' ? cfg.windFactor : 1.0;

        var rawT = cfg.transforms !== undefined ? cfg.transforms : (cfg.instances !== undefined ? cfg.instances : null);
        var count = 0;
        var baseTransforms = null;
        var transforms = null;

        if (rawT instanceof Float32Array) {
            count = (rawT.length / 16) | 0;
            transforms = new Float32Array(rawT);
            baseTransforms = new Float32Array(rawT);
        } else if (Array.isArray(rawT)) {
            if (rawT.length > 0 && typeof rawT[0] === 'number') {
                count = (rawT.length / 16) | 0;
                transforms = new Float32Array(rawT);
                baseTransforms = new Float32Array(rawT);
            } else if (rawT.length > 0 && Array.isArray(rawT[0])) {
                if (rawT[0].length === 16) {
                    count = rawT.length;
                    transforms = new Float32Array(count * 16);
                    for (var i = 0; i < count; i++) {
                        var m = rawT[i];
                        for (var j = 0; j < 16; j++) transforms[i * 16 + j] = m[j];
                    }
                    baseTransforms = new Float32Array(transforms);
                } else if (rawT[0].length >= 3) {
                    count = rawT.length;
                    transforms = new Float32Array(count * 16);
                    for (var i = 0; i < count; i++) {
                        var p = rawT[i];
                        var o = i * 16;
                        transforms[o + 0] = 1; transforms[o + 1] = 0; transforms[o + 2] = 0; transforms[o + 3] = p[0];
                        transforms[o + 4] = 0; transforms[o + 5] = 1; transforms[o + 6] = 0; transforms[o + 7] = p[1];
                        transforms[o + 8] = 0; transforms[o + 9] = 0; transforms[o + 10] = 1; transforms[o + 11] = p[2];
                        transforms[o + 12] = 0; transforms[o + 13] = 0; transforms[o + 14] = 0; transforms[o + 15] = 1;
                    }
                    baseTransforms = new Float32Array(transforms);
                }
            }
        } else if (typeof cfg.count === 'number' && cfg.count > 0) {
            count = cfg.count | 0;
            transforms = new Float32Array(count * 16);
            for (var i = 0; i < count; i++) {
                var o = i * 16;
                transforms[o + 0] = 1; transforms[o + 5] = 1; transforms[o + 10] = 1; transforms[o + 15] = 1;
            }
            baseTransforms = new Float32Array(transforms);
        }

        return {
            id: id,
            mesh: mesh,
            material: material,
            transforms: transforms,
            baseTransforms: baseTransforms,
            count: count,
            instanceCount: count,
            windFactor: windFactor,
            castShadow: cfg.castShadow !== undefined ? Boolean(cfg.castShadow) : true,
            receiveShadow: cfg.receiveShadow !== undefined ? Boolean(cfg.receiveShadow) : true,
            aabb: cfg.aabb || null,
            _swayed: false
        };
    }

    function _updateBatches() {
        var str = _windState.strength;
        for (var b = 0; b < _activeBatches.length; b++) {
            var batch = _activeBatches[b];
            if (!batch || !batch.count || !batch.baseTransforms || !batch.transforms) continue;
            var wFactor = batch.windFactor !== undefined ? batch.windFactor : 1.0;
            var effStrength = str * wFactor;
            var cur = batch.transforms;
            var base = batch.baseTransforms;
            if (effStrength === 0) {
                if (batch._swayed) {
                    for (var k = 0; k < base.length; k++) cur[k] = base[k];
                    batch._swayed = false;
                }
                continue;
            }
            batch._swayed = true;
            var dx = _windState.dirX || 0;
            var dz = _windState.dirY || 0;
            var dlen = Math.hypot(dx, dz);
            if (dlen > 1e-6) { dx /= dlen; dz /= dlen; } else { dx = 1.0; dz = 0.0; }
            var t = _windTime;
            for (var i = 0; i < batch.count; i++) {
                var o = i * 16;
                var px = base[o + 3];
                var py = base[o + 7];
                var pz = base[o + 11];
                var h = py > 0 ? py : 0;
                var hFactor = 0.05 + 0.04 * h + 0.015 * h * h;
                var phase = px * 0.4 + pz * 0.4;
                var wave = Math.sin(t * 2.8 + phase) * 0.7 + Math.sin(t * 5.2 + phase * 1.7) * 0.3;
                var sway = effStrength * hFactor * (1.0 + 0.6 * wave);
                var offX = dx * sway;
                var offZ = dz * sway;
                var offY = -0.05 * (offX * offX + offZ * offZ) / (h + 0.1);

                cur[o + 0] = base[o + 0] + dx * sway * 0.03;
                cur[o + 3] = px + offX;
                cur[o + 7] = py + offY;
                cur[o + 8] = base[o + 8] + dz * sway * 0.03;
                cur[o + 11] = pz + offZ;
            }
        }
    }

    fn(ns_flora, "setWind", function setWind(strength, dirX, dirY) {
        if (strength === undefined) throw new TypeError("bro.flora.setWind: strength is required");
        dirX = dirX === undefined ? 0 : dirX;
        dirY = dirY === undefined ? 0 : dirY;
        _windState.strength = strength;
        _windState.dirX = dirX;
        _windState.dirY = dirY;
        if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.setWind === 'function') {
            globalThis.__bro_native.flora.setWind(strength, dirX, dirY);
        }
        if (typeof __bro_flora_native !== 'undefined' && typeof __bro_flora_native.setWind === 'function') {
            __bro_flora_native.setWind(strength, dirX, dirY);
        }
    });

    fn(ns_flora, "wind", function wind(strength, dirX, dirY) {
        if (strength === undefined) {
            if (typeof __bro_flora_native !== 'undefined' && typeof __bro_flora_native.wind === 'function') {
                return __bro_flora_native.wind();
            }
            return { strength: _windState.strength, dirX: _windState.dirX, dirY: _windState.dirY };
        }
        dirX = dirX === undefined ? 0 : dirX;
        dirY = dirY === undefined ? 0 : dirY;
        _windState.strength = strength;
        _windState.dirX = dirX;
        _windState.dirY = dirY;
        if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.wind === 'function') {
            globalThis.__bro_native.flora.wind(strength, dirX, dirY);
        }
        if (typeof __bro_flora_native !== 'undefined' && typeof __bro_flora_native.wind === 'function') {
            __bro_flora_native.wind(strength, dirX, dirY);
        }
    });

    fn(ns_flora, "setDensity", function setDensity(density) {
        if (density === undefined) throw new TypeError("bro.flora.setDensity: density is required");
        _densityState = density;
        if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.setDensity === 'function') {
            globalThis.__bro_native.flora.setDensity(density);
        }
        if (typeof __bro_flora_native !== 'undefined' && typeof __bro_flora_native.setDensity === 'function') {
            __bro_flora_native.setDensity(density);
        }
    });

    fn(ns_flora, "density", function density(val) {
        if (val === undefined) {
            if (typeof __bro_flora_native !== 'undefined' && typeof __bro_flora_native.density === 'function') {
                return __bro_flora_native.density();
            }
            return _densityState;
        }
        _densityState = val;
        if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.density === 'function') {
            globalThis.__bro_native.flora.density(val);
        }
        if (typeof __bro_flora_native !== 'undefined' && typeof __bro_flora_native.density === 'function') {
            __bro_flora_native.density(val);
        }
    });

    fn(ns_flora, "update", function update(dt) {
        if (dt === undefined) throw new TypeError("bro.flora.update: dt is required");
        _windTime += dt;
        if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.update === 'function') {
            globalThis.__bro_native.flora.update(dt);
        }
        if (typeof __bro_flora_native !== 'undefined' && typeof __bro_flora_native.update === 'function') {
            __bro_flora_native.update(dt);
        }
        _updateBatches();
    });

    fn(ns_flora, "clear", function clear() {
        _windState.strength = 0;
        _windState.dirX = 0;
        _windState.dirY = 0;
        _windTime = 0;
        _densityState = 1.0;
        _activePlacements.length = 0;
        _activeBatches.length = 0;
        if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.clear === 'function') {
            globalThis.__bro_native.flora.clear();
        }
        if (typeof __bro_flora_native !== 'undefined' && typeof __bro_flora_native.clear === 'function') {
            __bro_flora_native.clear();
        }
    });

    fn(ns_flora, "placement", function placement(config) {
        if (config === undefined) return _activePlacements;
        if (Array.isArray(config)) {
            _activePlacements.length = 0;
            _activeBatches.length = 0;
            for (var i = 0; i < config.length; i++) {
                ns_flora.addPlacement(config[i]);
            }
            return _activeBatches;
        }
        return ns_flora.addPlacement(config);
    });

    fn(ns_flora, "addPlacement", function addPlacement(config) {
        if (!config) return null;
        if (Array.isArray(config)) {
            var res = [];
            for (var i = 0; i < config.length; i++) {
                res.push(ns_flora.addPlacement(config[i]));
            }
            return res;
        }
        var batch = _createBatch(config);
        if (batch) {
            _activePlacements.push(config);
            _activeBatches.push(batch);
        }
        return batch;
    });

    fn(ns_flora, "batches", function batches() {
        return _activeBatches;
    });

    fn(ns_flora, "getBatches", function getBatches() {
        return _activeBatches;
    });

    const phylObj = {
        alternate: 0, opposite: 1, spiral: 2, fascicle: 3, compoundPinnate: 4,
        Alternate: 0, Opposite: 1, Spiral: 2, Fascicle: 3, CompoundPinnate: 4,
    };
    fn(ns_flora, "phyllotaxy", phylObj);
    fn(ns_flora, "Phyllotaxy", phylObj);

    fn(ns_flora, "prototypes", {
        straight: function straight() { return __bro_flora_native.protoStraight(); },
        fork: function fork() { return __bro_flora_native.protoFork(); },
        whorl: function whorl(arms, spread) { return __bro_flora_native.protoWhorl(arms, spread); },
        monopodial: function monopodial(lateralBranches, lateralSpread) { return __bro_flora_native.protoMonopodial(lateralBranches, lateralSpread); },
        sympodial: function sympodial(primarySpread, lateralSpread) { return __bro_flora_native.protoSympodial(primarySpread, lateralSpread); },
        horizontalTier: function horizontalTier(arms, spread) { return __bro_flora_native.protoHorizontalTier(arms, spread); },
        tier: function tier(arms, spread) { return __bro_flora_native.protoHorizontalTier(arms, spread); },
        weeping: function weeping(spread, droop) { return __bro_flora_native.protoWeeping(spread, droop); },
    });

    // ---- bro.flora.FloraWorld ------------------------------------------------
    function FloraWorld() {
        throw new TypeError("bro.flora.FloraWorld is not constructible: instances come from the natives that return one");
    }
    fn(ns_flora, "FloraWorld", FloraWorld);
    globalThis.FloraWorld = FloraWorld;

    if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && globalThis.__bro_native.flora.FloraWorldProto !== undefined) {
        Object.setPrototypeOf(globalThis.__bro_native.flora.FloraWorldProto, FloraWorld.prototype);
    }

    fn(FloraWorld.prototype, "addPrototype", function addPrototype(spec) {
        return __bro_flora_native.addPrototype(this, spec);
    });

    fn(FloraWorld.prototype, "addVoronoiSite", function addVoronoiSite(prototypeIndex, determinacy, apicalControl) {
        if (prototypeIndex === undefined) throw new TypeError("bro.flora.FloraWorld.prototype.addVoronoiSite: prototypeIndex is required");
        if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.FloraWorld_addVoronoiSite === 'function') {
            return globalThis.__bro_native.flora.FloraWorld_addVoronoiSite(this, prototypeIndex, determinacy === undefined ? 1 : determinacy, apicalControl === undefined ? 0.5 : apicalControl);
        }
        return __bro_flora_native.addVoronoiSite(this, prototypeIndex, determinacy === undefined ? 1 : determinacy, apicalControl === undefined ? 0.5 : apicalControl);
    });

    fn(FloraWorld.prototype, "addPlant", function addPlant(spec) {
        return __bro_flora_native.addPlant(this, spec);
    });

    fn(FloraWorld.prototype, "removePlant", function removePlant(plantIdx) {
        if (plantIdx === undefined) throw new TypeError("bro.flora.FloraWorld.prototype.removePlant: plantIdx is required");
        if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.FloraWorld_removePlant === 'function') {
            return globalThis.__bro_native.flora.FloraWorld_removePlant(this, plantIdx);
        }
        return __bro_flora_native.removePlant(this, plantIdx);
    });

    fn(FloraWorld.prototype, "step", function step(dt) {
        if (dt === undefined) throw new TypeError("bro.flora.FloraWorld.prototype.step: dt is required");
        if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.FloraWorld_step === 'function') {
            return globalThis.__bro_native.flora.FloraWorld_step(this, dt);
        }
        return __bro_flora_native.step(this, dt);
    });

    fn(FloraWorld.prototype, "plantInfo", function plantInfo(plantIdx) {
        return __bro_flora_native.plantInfo(this, plantIdx);
    });

    fn(FloraWorld.prototype, "setClimate", function setClimate(opts) {
        return __bro_flora_native.setClimate(this, opts);
    });

    fn(FloraWorld.prototype, "sampleShadow", function sampleShadow(pos) {
        return __bro_flora_native.sampleShadow(this, pos);
    });

    fn(FloraWorld.prototype, "validate", function validate() {
        return __bro_flora_native.validate(this);
    });

    fn(FloraWorld.prototype, "emitMesh", function emitMesh(sides) {
        return __bro_flora_native.emitMesh(this, sides === undefined ? 6 : sides);
    });

    fn(FloraWorld.prototype, "emitSegments", function emitSegments() {
        return __bro_flora_native.emitSegments(this);
    });

    fn(FloraWorld.prototype, "emitFoliage", function emitFoliage() {
        return __bro_flora_native.emitFoliage(this);
    });

    fn(FloraWorld.prototype, "emitBloomAnchors", function emitBloomAnchors() {
        return __bro_flora_native.emitBloomAnchors(this);
    });

    fn(FloraWorld.prototype, "emitPlantMesh", function emitPlantMesh(plantIdx, sides) {
        return __bro_flora_native.emitPlantMesh(this, plantIdx, sides === undefined ? 6 : sides);
    });

    fn(FloraWorld.prototype, "emitPlantSegments", function emitPlantSegments(plantIdx) {
        return __bro_flora_native.emitPlantSegments(this, plantIdx);
    });

    fn(FloraWorld.prototype, "emitPlantFoliage", function emitPlantFoliage(plantIdx) {
        return __bro_flora_native.emitPlantFoliage(this, plantIdx);
    });

    fn(FloraWorld.prototype, "emitPlantBloomAnchors", function emitPlantBloomAnchors(plantIdx) {
        return __bro_flora_native.emitPlantBloomAnchors(this, plantIdx);
    });

    fn(FloraWorld.prototype, "emitFoliageTransforms", function emitFoliageTransforms(opts) {
        return __bro_flora_native.emitFoliageTransforms(this, opts);
    });

    fn(FloraWorld.prototype, "emitSegmentTransforms", function emitSegmentTransforms() {
        return __bro_flora_native.emitSegmentTransforms(this);
    });

    fn(FloraWorld.prototype, "emitScatterSegments", function emitScatterSegments(opts) {
        return __bro_flora_native.emitScatterSegments(this, opts);
    });

    fn(FloraWorld.prototype, "emitBranchTubes", function emitBranchTubes(opts) {
        return __bro_flora_native.emitBranchTubes(this, opts);
    });

    fn(FloraWorld.prototype, "emitFoliageMesh", function emitFoliageMesh(leafMesh, opts) {
        return __bro_flora_native.emitFoliageMesh(this, leafMesh, opts);
    });

    fn(FloraWorld.prototype, "emitPlantFoliageMesh", function emitPlantFoliageMesh(plantIdx, leafMesh, opts) {
        return __bro_flora_native.emitPlantFoliageMesh(this, plantIdx, leafMesh, opts);
    });

    fn(FloraWorld.prototype, "emitBloomMesh", function emitBloomMesh(petalMesh, centerMesh, opts) {
        if (petalMesh === undefined) throw new TypeError("bro.flora.FloraWorld.prototype.emitBloomMesh: petalMesh is required");
        return __bro_flora_native.emitBloomMesh(this, petalMesh, centerMesh === undefined ? null : centerMesh, opts);
    });

    fn(FloraWorld.prototype, "emitPlantSdfMesh", function emitPlantSdfMesh(plantIdx, opts) {
        return __bro_flora_native.emitPlantSdfMesh(this, plantIdx, opts);
    });

    fn(FloraWorld.prototype, "emitWorldSdfMesh", function emitWorldSdfMesh(opts) {
        return __bro_flora_native.emitWorldSdfMesh(this, opts);
    });

    accessor(FloraWorld.prototype, "simTime",
        function () {
            if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.FloraWorld_simTime_get === 'function') {
                return globalThis.__bro_native.flora.FloraWorld_simTime_get(this);
            }
            return __bro_flora_native.simTime(this);
        },
        undefined);

    accessor(FloraWorld.prototype, "plantCount",
        function () {
            if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.FloraWorld_plantCount_get === 'function') {
                return globalThis.__bro_native.flora.FloraWorld_plantCount_get(this);
            }
            return __bro_flora_native.plantCount(this);
        },
        undefined);

    accessor(FloraWorld.prototype, "prototypeCount",
        function () {
            if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.FloraWorld_prototypeCount_get === 'function') {
                return globalThis.__bro_native.flora.FloraWorld_prototypeCount_get(this);
            }
            return __bro_flora_native.prototypeCount(this);
        },
        undefined);

    accessor(FloraWorld.prototype, "moduleCount",
        function () {
            if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.FloraWorld_moduleCount_get === 'function') {
                return globalThis.__bro_native.flora.FloraWorld_moduleCount_get(this);
            }
            return __bro_flora_native.moduleCount(this);
        },
        undefined);
})();
