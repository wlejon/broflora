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
    var _activeBatches = [];
    var _activePlacements = [];
    var _nextBatchId = 0;

    // One instance in bro's InstancedMeshNode layout, the layout the native
    // emitters write: rows 0-2 are a row-major 3x4 affine (identity basis,
    // translation at floats 3 / 7 / 11) and floats 12-15 are the instance's
    // RGBA tint, white. It is not the bottom row of a 4x4: 0, 0, 0, 1 there
    // would draw the instance black.
    function _writeInstance(t, o, x, y, z) {
        t[o + 0] = 1; t[o + 1] = 0; t[o + 2] = 0;  t[o + 3] = x;
        t[o + 4] = 0; t[o + 5] = 1; t[o + 6] = 0;  t[o + 7] = y;
        t[o + 8] = 0; t[o + 9] = 0; t[o + 10] = 1; t[o + 11] = z;
        t[o + 12] = 1; t[o + 13] = 1; t[o + 14] = 1; t[o + 15] = 1;
    }

    function _createBatch(cfg) {
        if (!cfg) return null;
        var id = cfg.id !== undefined ? cfg.id : ('flora_batch_' + (++_nextBatchId));
        var mesh = cfg.mesh !== undefined ? cfg.mesh : null;
        var material = cfg.material !== undefined ? cfg.material : null;
        var windFactor = typeof cfg.windFactor === 'number' ? cfg.windFactor : 1.0;

        var rawT = cfg.transforms !== undefined ? cfg.transforms : (cfg.instances !== undefined ? cfg.instances : null);
        // A batch holds at most 2^24 instances, the cap every flora list has:
        // past it the copies below would size gigabyte buffers.
        if (rawT && typeof rawT.length === 'number') {
            var claimed = (Array.isArray(rawT) && rawT.length > 0 && typeof rawT[0] !== 'number')
                ? rawT.length : rawT.length / 16;
            if (claimed > 16777216) {
                throw new RangeError("bro.flora.addPlacement: config.transforms holds " + Math.floor(claimed) +
                                     " instances, over the 16777216 limit");
            }
        }
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
                        _writeInstance(transforms, i * 16, p[0], p[1], p[2]);
                    }
                    baseTransforms = new Float32Array(transforms);
                }
            }
        } else if (cfg.count !== undefined) {
            // `| 0` used to wrap a big count and a fraction truncated silently.
            if (typeof cfg.count !== 'number') {
                throw new TypeError("bro.flora.addPlacement: config.count must be a number");
            }
            if (cfg.count !== Math.floor(cfg.count) || cfg.count < 0 || cfg.count > 16777216) {
                throw new RangeError("bro.flora.addPlacement: config.count must be an integer in [0, 16777216], got " + cfg.count);
            }
            count = cfg.count;
            transforms = new Float32Array(count * 16);
            for (var i = 0; i < count; i++) _writeInstance(transforms, i * 16, 0, 0, 0);
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

    // Re-sway every batch from its rest pose. The bend itself is the native
    // wind model (swayTransforms), the same one the emitters use, so a
    // batch built from emitFoliageTransforms output sways exactly as the
    // emitter's own wind-bent matrices do.
    function _updateBatches() {
        var str = _windState.strength;
        var canSway = !!(_native && typeof _native.swayTransforms === 'function');
        for (var b = 0; b < _activeBatches.length; b++) {
            var batch = _activeBatches[b];
            if (!batch || !batch.count || !batch.baseTransforms || !batch.transforms) continue;
            var wFactor = batch.windFactor !== undefined ? batch.windFactor : 1.0;
            var cur = batch.transforms;
            var base = batch.baseTransforms;
            if (str * wFactor === 0 || !canSway) {
                if (batch._swayed) {
                    for (var k = 0; k < base.length; k++) cur[k] = base[k];
                    batch._swayed = false;
                }
                continue;
            }
            batch._swayed = true;
            _native.swayTransforms(base, cur, wFactor);
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
        if (_native && typeof _native.update === 'function') {
            _native.update(dt);
        }
        _updateBatches();
    });

    fn(ns_flora, "clear", function clear() {
        _windState.strength = 0;
        _windState.dirX = 0;
        _windState.dirY = 0;
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
