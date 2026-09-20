(function () {
    'use strict';

    const accessor = (obj, name, get, set) =>
        Object.defineProperty(obj, name, { get, set, enumerable: true, configurable: true });
    const fn = (obj, name, value) =>
        Object.defineProperty(obj, name, { value, writable: true, enumerable: true, configurable: true });
    const mount = (root, name) => root[name] !== undefined ? root[name] : (root[name] = {});

    const _native = (typeof __bro_flora_native !== 'undefined' && __bro_flora_native) ? __bro_flora_native :
                    ((typeof globalThis !== 'undefined' && globalThis.__bro_native && globalThis.__bro_native.flora) ? globalThis.__bro_native.flora : null);

    // ---- bro.flora -----------------------------------------------------------
    const broObj = mount(globalThis, "bro");
    const ns_flora = mount(broObj, "flora");
    accessor(ns_flora, "available", function () { return true; }, undefined);

    fn(ns_flora, "createWorld", function createWorld(opts) {
        var w = _native ? _native.createWorld(opts) : null;
        if (w && Object.getPrototypeOf(w) !== FloraWorld.prototype) {
            Object.setPrototypeOf(w, FloraWorld.prototype);
        }
        return w;
    });

    fn(ns_flora, "leafCluster", function leafCluster(phyl, opts) {
        return _native ? _native.leafCluster(phyl, opts) : null;
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

                // Rigid rotation around axis u = (dz, 0, -dx)
                var angle = sway * 0.05;
                var c = Math.cos(angle);
                var s = Math.sin(angle);
                var omc = 1.0 - c;

                var r00 = c + dz * dz * omc;
                var r01 = dx * s;
                var r02 = -dx * dz * omc;

                var r10 = -dx * s;
                var r11 = c;
                var r12 = -dz * s;

                var r20 = -dx * dz * omc;
                var r21 = dz * s;
                var r22 = c + dx * dx * omc;

                var b00 = base[o + 0], b01 = base[o + 1], b02 = base[o + 2];
                var b10 = base[o + 4], b11 = base[o + 5], b12 = base[o + 6];
                var b20 = base[o + 8], b21 = base[o + 9], b22 = base[o + 10];

                cur[o + 0] = r00 * b00 + r01 * b10 + r02 * b20;
                cur[o + 1] = r00 * b01 + r01 * b11 + r02 * b21;
                cur[o + 2] = r00 * b02 + r01 * b12 + r02 * b22;
                cur[o + 3] = px + offX;

                cur[o + 4] = r10 * b00 + r11 * b10 + r12 * b20;
                cur[o + 5] = r10 * b01 + r11 * b11 + r12 * b21;
                cur[o + 6] = r10 * b02 + r11 * b12 + r12 * b22;
                cur[o + 7] = py + offY;

                cur[o + 8] = r20 * b00 + r21 * b10 + r22 * b20;
                cur[o + 9] = r20 * b01 + r21 * b11 + r22 * b21;
                cur[o + 10] = r20 * b02 + r21 * b12 + r22 * b22;
                cur[o + 11] = pz + offZ;

                cur[o + 12] = base[o + 12];
                cur[o + 13] = base[o + 13];
                cur[o + 14] = base[o + 14];
                cur[o + 15] = base[o + 15];
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
        if (_native && typeof _native.setWind === 'function') {
            _native.setWind(strength, dirX, dirY);
        }
    });

    fn(ns_flora, "wind", function wind(strength, dirX, dirY) {
        if (strength === undefined) {
            if (_native && typeof _native.wind === 'function') {
                return _native.wind();
            }
            return { strength: _windState.strength, dirX: _windState.dirX, dirY: _windState.dirY };
        }
        dirX = dirX === undefined ? 0 : dirX;
        dirY = dirY === undefined ? 0 : dirY;
        _windState.strength = strength;
        _windState.dirX = dirX;
        _windState.dirY = dirY;
        if (_native && typeof _native.wind === 'function') {
            _native.wind(strength, dirX, dirY);
        }
    });

    fn(ns_flora, "setDensity", function setDensity(density) {
        if (density === undefined) throw new TypeError("bro.flora.setDensity: density is required");
        _densityState = density;
        if (_native && typeof _native.setDensity === 'function') {
            _native.setDensity(density);
        }
    });

    fn(ns_flora, "density", function density(val) {
        if (val === undefined) {
            if (_native && typeof _native.density === 'function') {
                return _native.density();
            }
            return _densityState;
        }
        _densityState = val;
        if (_native && typeof _native.density === 'function') {
            _native.density(val);
        }
    });

    fn(ns_flora, "update", function update(dt) {
        if (dt === undefined) throw new TypeError("bro.flora.update: dt is required");
        _windTime += dt;
        if (_native && typeof _native.update === 'function') {
            _native.update(dt);
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
        if (_native && typeof _native.clear === 'function') {
            _native.clear();
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
        straight: function straight() { return _native ? _native.protoStraight() : null; },
        fork: function fork() { return _native ? _native.protoFork() : null; },
        whorl: function whorl(arms, spread) { return _native ? _native.protoWhorl(arms, spread) : null; },
        monopodial: function monopodial(lateralBranches, lateralSpread) { return _native ? _native.protoMonopodial(lateralBranches, lateralSpread) : null; },
        sympodial: function sympodial(primarySpread, lateralSpread) { return _native ? _native.protoSympodial(primarySpread, lateralSpread) : null; },
        horizontalTier: function horizontalTier(arms, spread) { return _native ? _native.protoHorizontalTier(arms, spread) : null; },
        tier: function tier(arms, spread) { return _native ? _native.protoHorizontalTier(arms, spread) : null; },
        weeping: function weeping(spread, droop) { return _native ? _native.protoWeeping(spread, droop) : null; },
    });

    // ---- bro.flora.FloraWorld ------------------------------------------------
    function FloraWorld() {
        throw new TypeError("bro.flora.FloraWorld is not constructible: instances come from the natives that return one");
    }
    fn(ns_flora, "FloraWorld", FloraWorld);
    globalThis.FloraWorld = FloraWorld;

    if (_native && _native.FloraWorldProto !== undefined) {
        Object.setPrototypeOf(_native.FloraWorldProto, FloraWorld.prototype);
    }

    fn(FloraWorld.prototype, "addPrototype", function addPrototype(spec) {
        return _native ? _native.addPrototype(this, spec) : -1;
    });

    fn(FloraWorld.prototype, "addVoronoiSite", function addVoronoiSite(prototypeIndex, determinacy, apicalControl) {
        if (prototypeIndex === undefined) throw new TypeError("bro.flora.FloraWorld.prototype.addVoronoiSite: prototypeIndex is required");
        var det = determinacy === undefined ? 1 : determinacy;
        var ap = apicalControl === undefined ? 0.5 : apicalControl;
        if (_native) {
            if (typeof _native.addVoronoiSite === 'function') return _native.addVoronoiSite(this, prototypeIndex, det, ap);
            if (typeof _native.FloraWorld_addVoronoiSite === 'function') return _native.FloraWorld_addVoronoiSite(this, prototypeIndex, det, ap);
        }
        return 0;
    });

    fn(FloraWorld.prototype, "addPlant", function addPlant(spec) {
        return _native ? _native.addPlant(this, spec) : -1;
    });

    fn(FloraWorld.prototype, "removePlant", function removePlant(plantIdx) {
        if (plantIdx === undefined) throw new TypeError("bro.flora.FloraWorld.prototype.removePlant: plantIdx is required");
        if (_native) {
            if (typeof _native.removePlant === 'function') return _native.removePlant(this, plantIdx);
            if (typeof _native.FloraWorld_removePlant === 'function') return _native.FloraWorld_removePlant(this, plantIdx);
        }
        return false;
    });

    fn(FloraWorld.prototype, "step", function step(dt) {
        if (dt === undefined) throw new TypeError("bro.flora.FloraWorld.prototype.step: dt is required");
        if (_native) {
            if (typeof _native.step === 'function') return _native.step(this, dt);
            if (typeof _native.FloraWorld_step === 'function') return _native.FloraWorld_step(this, dt);
        }
    });

    fn(FloraWorld.prototype, "plantInfo", function plantInfo(plantIdx) {
        return _native ? _native.plantInfo(this, plantIdx) : null;
    });

    fn(FloraWorld.prototype, "setClimate", function setClimate(opts) {
        return _native ? _native.setClimate(this, opts) : false;
    });

    fn(FloraWorld.prototype, "sampleShadow", function sampleShadow(pos) {
        return _native ? _native.sampleShadow(this, pos) : 0;
    });

    fn(FloraWorld.prototype, "validate", function validate() {
        return _native ? _native.validate(this) : null;
    });

    fn(FloraWorld.prototype, "emitMesh", function emitMesh(sides) {
        return _native ? _native.emitMesh(this, sides === undefined ? 6 : sides) : null;
    });

    fn(FloraWorld.prototype, "emitSegments", function emitSegments() {
        return _native ? _native.emitSegments(this) : null;
    });

    fn(FloraWorld.prototype, "emitFoliage", function emitFoliage() {
        return _native ? _native.emitFoliage(this) : null;
    });

    fn(FloraWorld.prototype, "emitBloomAnchors", function emitBloomAnchors() {
        return _native ? _native.emitBloomAnchors(this) : null;
    });

    fn(FloraWorld.prototype, "emitPlantMesh", function emitPlantMesh(plantIdx, sides) {
        return _native ? _native.emitPlantMesh(this, plantIdx, sides === undefined ? 6 : sides) : null;
    });

    fn(FloraWorld.prototype, "emitPlantSegments", function emitPlantSegments(plantIdx) {
        return _native ? _native.emitPlantSegments(this, plantIdx) : null;
    });

    fn(FloraWorld.prototype, "emitPlantFoliage", function emitPlantFoliage(plantIdx) {
        return _native ? _native.emitPlantFoliage(this, plantIdx) : null;
    });

    fn(FloraWorld.prototype, "emitPlantBloomAnchors", function emitPlantBloomAnchors(plantIdx) {
        return _native ? _native.emitPlantBloomAnchors(this, plantIdx) : null;
    });

    fn(FloraWorld.prototype, "emitFoliageTransforms", function emitFoliageTransforms(opts) {
        return _native ? _native.emitFoliageTransforms(this, opts) : null;
    });

    fn(FloraWorld.prototype, "emitSegmentTransforms", function emitSegmentTransforms() {
        return _native ? _native.emitSegmentTransforms(this) : null;
    });

    fn(FloraWorld.prototype, "emitScatterSegments", function emitScatterSegments(opts) {
        return _native ? _native.emitScatterSegments(this, opts) : null;
    });

    fn(FloraWorld.prototype, "emitBranchTubes", function emitBranchTubes(opts) {
        return _native ? _native.emitBranchTubes(this, opts) : null;
    });

    fn(FloraWorld.prototype, "emitFoliageMesh", function emitFoliageMesh(leafMesh, opts) {
        return _native ? _native.emitFoliageMesh(this, leafMesh, opts) : null;
    });

    fn(FloraWorld.prototype, "emitPlantFoliageMesh", function emitPlantFoliageMesh(plantIdx, leafMesh, opts) {
        return _native ? _native.emitPlantFoliageMesh(this, plantIdx, leafMesh, opts) : null;
    });

    fn(FloraWorld.prototype, "emitBloomMesh", function emitBloomMesh(petalMesh, centerMesh, opts) {
        if (petalMesh === undefined) throw new TypeError("bro.flora.FloraWorld.prototype.emitBloomMesh: petalMesh is required");
        return _native ? _native.emitBloomMesh(this, petalMesh, centerMesh === undefined ? null : centerMesh, opts) : null;
    });

    fn(FloraWorld.prototype, "emitPlantSdfMesh", function emitPlantSdfMesh(plantIdx, opts) {
        return _native ? _native.emitPlantSdfMesh(this, plantIdx, opts) : null;
    });

    fn(FloraWorld.prototype, "emitWorldSdfMesh", function emitWorldSdfMesh(opts) {
        return _native ? _native.emitWorldSdfMesh(this, opts) : null;
    });

    accessor(FloraWorld.prototype, "simTime",
        function () {
            if (_native) {
                if (typeof _native.simTime === 'function') return _native.simTime(this);
                if (typeof _native.FloraWorld_simTime_get === 'function') return _native.FloraWorld_simTime_get(this);
            }
            return 0;
        },
        undefined);

    accessor(FloraWorld.prototype, "plantCount",
        function () {
            if (_native) {
                if (typeof _native.plantCount === 'function') return _native.plantCount(this);
                if (typeof _native.FloraWorld_plantCount_get === 'function') return _native.FloraWorld_plantCount_get(this);
            }
            return 0;
        },
        undefined);

    accessor(FloraWorld.prototype, "prototypeCount",
        function () {
            if (_native) {
                if (typeof _native.prototypeCount === 'function') return _native.prototypeCount(this);
                if (typeof _native.FloraWorld_prototypeCount_get === 'function') return _native.FloraWorld_prototypeCount_get(this);
            }
            return 0;
        },
        undefined);

    accessor(FloraWorld.prototype, "moduleCount",
        function () {
            if (_native) {
                if (typeof _native.moduleCount === 'function') return _native.moduleCount(this);
                if (typeof _native.FloraWorld_moduleCount_get === 'function') return _native.FloraWorld_moduleCount_get(this);
            }
            return 0;
        },
        undefined);
})();
