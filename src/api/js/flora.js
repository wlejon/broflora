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

    // ---- Forests -------------------------------------------------------------

    // A 32-bit hash to [0, 1) (triple32-lite).
    function _hash01(x) {
        x = x >>> 0;
        x ^= x >>> 16; x = Math.imul(x, 0x7feb352d);
        x ^= x >>> 15; x = Math.imul(x, 0x846ca68b);
        x ^= x >>> 16;
        return (x >>> 0) / 4294967296;
    }
    function _cellHash(i, j, seed, k) {
        return _hash01(Math.imul(i, 0x27d4eb2d) ^ Math.imul(j, 0x165667b1) ^ Math.imul(seed + k, 0x9e3779b9));
    }
    function _numberOr(v, d) { return typeof v === 'number' && isFinite(v) ? v : d; }

    // Instances for a forest emit (world.emitForest): its specimens planted
    // many times over an area, x, y, z, scale, yaw, specimen, 0, 0 each (the
    // layout scene.createForest's `instances` takes; yaw turns a specimen
    // about +Y: local (x, z) -> (x cos + z sin, -x sin + z cos)).
    //   area      [x0, z0, x1, z1] (world metres)
    //   spacing   mean distance between neighbouring trees (default 7 m)
    //   jitter    how far a tree strays from its grid cell's centre, in
    //             spacings (default 0.42)
    //   ground    (x, z) -> y, the height the trees stand on (default 0)
    //   density   number or (x, z) -> 0..1: the chance a cell holds a tree
    //   scale     the node scale the specimens are drawn at, a number or
    //             [min, max] (default 1)
    //   sink      how far a root sits below the ground (default 0.1 m)
    //   specimens which specimens to use (indices; default all), or
    //             weights per specimen
    //   seed
    // A mixed forest: `forest` an array of emits (one per leaf form, say, or
    // per species) and `mix(x, z, y)` -> a weight per forest (or the index
    // of one) at that point: by elevation, by patch. Each cell then holds a
    // tree of the forest its weights pick, and the answer is an array of
    // instance arrays, one per forest. `specimens` and `scale` may then be
    // arrays with an entry per forest (`scale: [[1, 1.2], 0.9]`).
    function _specimenPool(count, sel) {
        const pool = [];
        if (Array.isArray(sel) && sel.length === count && sel.every((v) => typeof v === 'number' && v <= 1)) {
            // Weights per specimen.
            let sum = 0;
            for (let i = 0; i < count; i++) sum += Math.max(0, sel[i]);
            let acc = 0;
            for (let i = 0; i < count; i++) { acc += Math.max(0, sel[i]) / (sum || 1); pool.push([acc, i]); }
        } else {
            const ids = Array.isArray(sel) ? sel.filter((i) => i >= 0 && i < count) : [...Array(count).keys()];
            ids.forEach((id, k) => pool.push([(k + 1) / ids.length, id]));
        }
        return pool;
    }
    function _scaleRange(v) {
        return Array.isArray(v) ? [_numberOr(v[0], 1), _numberOr(v[1], _numberOr(v[0], 1))] : [_numberOr(v, 1), _numberOr(v, 1)];
    }
    fn(ns_flora, "plantForest", function plantForest(forest, opts) {
        const o = opts || {};
        const mixed = Array.isArray(forest);
        const forests = mixed ? forest : [forest];
        const area = o.area || [-50, -50, 50, 50];
        const spacing = Math.max(0.5, _numberOr(o.spacing, 7));
        const jitter = _numberOr(o.jitter, 0.42);
        const ground = typeof o.ground === 'function' ? o.ground : () => 0;
        const density = typeof o.density === 'function' ? o.density : () => _numberOr(o.density, 1);
        const sink = _numberOr(o.sink, 0.1);
        const seed = _numberOr(o.seed, 1) | 0;
        const perForest = (v, k) => (mixed && Array.isArray(v) && v.length === forests.length ? v[k] : v);
        const pools = forests.map((f, k) => {
            const count = f && f.specimenCount ? f.specimenCount | 0 : 0;
            return count > 0 ? _specimenPool(count, perForest(o.specimens, k)) : [];
        });
        const scales = forests.map((f, k) => _scaleRange(perForest(o.scale, k)));
        const outs = forests.map(() => []);
        const mix = typeof o.mix === 'function' ? o.mix : null;
        // The forest a cell's u in [0, 1) picks from the weights at (x, z).
        const pickForest = (x, z, y, u) => {
            if (!mix || forests.length === 1) return pools[0].length ? 0 : -1;
            const w = mix(x, z, y);
            if (typeof w === 'number') return w >= 0 && w < forests.length && pools[w | 0].length ? w | 0 : -1;
            let sum = 0;
            for (let k = 0; k < forests.length; k++) if (pools[k].length) sum += Math.max(0, _numberOr(w && w[k], 0));
            if (!(sum > 0)) return -1;
            let acc = 0;
            for (let k = 0; k < forests.length; k++) {
                if (!pools[k].length) continue;
                acc += Math.max(0, _numberOr(w && w[k], 0)) / sum;
                if (u < acc) return k;
            }
            for (let k = forests.length; k-- > 0;) if (pools[k].length && _numberOr(w && w[k], 0) > 0) return k;
            return -1;
        };
        const pick = (pool, u) => { for (const [c, id] of pool) if (u < c) return id; return pool[pool.length - 1][1]; };
        const i0 = Math.floor(area[0] / spacing), i1 = Math.ceil(area[2] / spacing);
        const j0 = Math.floor(area[1] / spacing), j1 = Math.ceil(area[3] / spacing);
        for (let j = j0; j <= j1; j++) {
            for (let i = i0; i <= i1; i++) {
                const x = (i + 0.5 + jitter * 2 * (_cellHash(i, j, seed, 1) - 0.5)) * spacing;
                const z = (j + 0.5 + jitter * 2 * (_cellHash(i, j, seed, 2) - 0.5)) * spacing;
                if (x < area[0] || x > area[2] || z < area[1] || z > area[3]) continue;
                if (_cellHash(i, j, seed, 3) >= Math.min(1, Math.max(0, density(x, z)))) continue;
                const y = ground(x, z);
                const k = pickForest(x, z, y, _cellHash(i, j, seed, 7));
                if (k < 0) continue;
                const scale = scales[k];
                const s = scale[0] + (scale[1] - scale[0]) * _cellHash(i, j, seed, 4);
                outs[k].push(x, y - sink, z, s, _cellHash(i, j, seed, 5) * 6.283185307, pick(pools[k], _cellHash(i, j, seed, 6)), 0, 0);
            }
        }
        const arrays = outs.map((a) => new Float32Array(a));
        return mixed ? arrays : arrays[0];
    });

    // Ground cover under a forest (scene.createGrass `density`): a function
    // (x, z) -> 0..1 that is 0 under a closed canopy, thin in the crowns'
    // shade, clear at each trunk, full in the open. `instances` defaults to
    // the forest's own. `floor` is the shade's least cover before the canopy
    // closes; `closed` the crown overlap at which nothing grows. Its
    // `canopy` member is the crowns' shade over a point (createGrass's
    // `floor`).
    // A mixed forest is `forest` an array of emits, `instances` (if given)
    // an array with each one's instances.
    fn(ns_flora, "forestCover", function forestCover(forest, opts) {
        const o = opts || {};
        const forests = Array.isArray(forest) ? forest : [forest];
        const insts = forests.map((f, k) => (Array.isArray(forest) ? (o.instances && o.instances[k]) : o.instances) ||
                                            (f && f.instances) || new Float32Array(0));
        const floor = _numberOr(o.floor, 0.35);
        const closed = _numberOr(o.closed, 1.6);
        const clear = _numberOr(o.trunkClear, 0.7);
        let n = 0;
        for (const inst of insts) n += (inst.length / 8) | 0;
        // Each tree as its trunk, crown centre and crown radius; the native
        // ForestCover (broflora/forest_cover.h) buckets them by 16 m cell and
        // answers each point, since the grass bakes ask once a texel,
        // hundreds of thousands of times.
        const trees = new Float64Array(n * 5);
        let m = 0;
        for (let k = 0; k < forests.length; k++) {
            const inst = insts[k];
            const spec = forests[k] && forests[k].specimens ? forests[k].specimens : new Float32Array(0);
            const count = (inst.length / 8) | 0;
            for (let i = 0; i < count; i++) {
                const sp = inst[i * 8 + 5] | 0, s = inst[i * 8 + 3];
                if (sp * 16 + 15 >= spec.length) continue;
                const cr = spec[sp * 16 + 7] * s, yaw = inst[i * 8 + 4];
                const cx = spec[sp * 16 + 8] * s, cz = spec[sp * 16 + 10] * s;
                trees[m * 5] = inst[i * 8];
                trees[m * 5 + 1] = inst[i * 8 + 2];
                trees[m * 5 + 2] = inst[i * 8] + cx * Math.cos(yaw) + cz * Math.sin(yaw);
                trees[m * 5 + 3] = inst[i * 8 + 2] - cx * Math.sin(yaw) + cz * Math.cos(yaw);
                trees[m * 5 + 4] = Math.max(cr, 0.5);
                m++;
            }
        }
        const handle = _native.forestCover(trees.subarray(0, m * 5), floor, closed, clear);
        const at = _native.forestCoverAt, canopyAt = _native.forestCanopyAt;
        const cover = function cover(x, z) { return at(handle, x, z); };
        // The crowns over a point, 0 in the open .. 1 under a crown's middle
        // or where crowns overlap: createGrass's `floor` (the leaf litter and
        // the low leafy plants of a forest floor).
        cover.canopy = function canopy(x, z) { return canopyAt(handle, x, z); };
        return cover;
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

    fn(FloraWorld.prototype, "addVoronoiSite", function addVoronoiSite(prototypeIndex, determinacy, apicalControl, group) {
        if (prototypeIndex === undefined) throw new TypeError("bro.flora.FloraWorld.prototype.addVoronoiSite: prototypeIndex is required");
        var det = determinacy === undefined ? 1 : determinacy;
        var ap = apicalControl === undefined ? 0.5 : apicalControl;
        if (_native) {
            if (typeof _native.addVoronoiSite === 'function') return _native.addVoronoiSite(this, prototypeIndex, det, ap, group);
            if (typeof _native.FloraWorld_addVoronoiSite === 'function') return _native.FloraWorld_addVoronoiSite(this, prototypeIndex, det, ap);
        }
        return 0;
    });

    // Species presets (broflora/presets.h): addSpecies(name) registers one's
    // modules in the world (again: the same ones) and answers {name, habit,
    // rootPrototype, species}; plantSpecies(name, {origin, age, species})
    // plants one, `species` a partial Species over the preset's.
    fn(ns_flora, "speciesPresets", function speciesPresets() {
        return _native && _native.speciesPresets ? _native.speciesPresets() : [];
    });

    fn(FloraWorld.prototype, "addSpecies", function addSpecies(name) {
        return _native && _native.addSpecies ? _native.addSpecies(this, '' + name) : null;
    });

    fn(FloraWorld.prototype, "plantSpecies", function plantSpecies(name, spec) {
        return _native && _native.plantSpecies ? _native.plantSpecies(this, '' + name, spec) : -1;
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

    // grow(ticks, dt, { cache }): `ticks` steps of `dt` in one native call;
    // with `cache` (a directory) the grown world is read from, or written
    // to, a cache keyed by the world, ticks, dt and the step's source.
    fn(FloraWorld.prototype, "grow", function grow(ticks, dt, opts) {
        if (ticks === undefined || dt === undefined) throw new TypeError("bro.flora.FloraWorld.prototype.grow: ticks and dt are required");
        var cache = opts && typeof opts.cache === 'string' ? opts.cache : '';
        return _native ? _native.grow(this, ticks, dt, cache) : undefined;
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

    // Where ground cover (bro.scene's createGrass `density`) grows among the
    // plants: a function (x, z, y) -> 0..1 in world metres. Grass thins in
    // the crowns' shade (the shadow grid's light just above the ground) and
    // clears at each trunk; `floor` is what is left at the darkest. `scale`
    // is the node scale the plants are drawn at (world = plant space x scale).
    fn(FloraWorld.prototype, "groundCover", function groundCover(opts) {
        const o = opts || {};
        const world = this;
        const scale = o.scale > 0 ? +o.scale : 1;
        const floor = typeof o.floor === 'number' ? o.floor : 0.3;
        const shade = typeof o.shade === 'number' ? o.shade : 0.6;
        const clear = typeof o.trunkClear === 'number' ? o.trunkClear : 0.7;
        const above = typeof o.above === 'number' ? o.above : 0.5;
        const roots = [];
        const n = world.plantCount | 0;
        for (let i = 0; i < n; i++) {
            const info = world.plantInfo(i);
            if (info && info.origin) roots.push([info.origin[0] * scale, info.origin[2] * scale]);
        }
        const smooth = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
        return function cover(x, z, y) {
            let d = 1;
            for (let i = 0; i < roots.length; i++)
                d = Math.min(d, smooth(clear, clear * 3.5, Math.hypot(x - roots[i][0], z - roots[i][1])));
            const L = world.sampleShadow([x / scale, ((+y || 0) + above) / scale, z / scale]);
            if (typeof L === 'number') d *= 1 - shade * (1 - Math.min(Math.max(L, 0), 1));
            return floor + (1 - floor) * d;
        };
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

    // Vegetation records (16 floats per instance) in lockstep with
    // emitFoliageTransforms(opts) / emitSegmentTransforms(): pass them as
    // createInstancedMesh's `vegetation` and the instances sway in the
    // scene's wind.
    fn(FloraWorld.prototype, "emitFoliageVegetation", function emitFoliageVegetation(opts) {
        return _native ? _native.emitFoliageVegetation(this, opts) : null;
    });

    fn(FloraWorld.prototype, "emitSegmentVegetation", function emitSegmentVegetation() {
        return _native ? _native.emitSegmentVegetation(this) : null;
    });

    fn(FloraWorld.prototype, "emitClumps", function emitClumps(opts) {
        return _native ? _native.emitClumps(this, opts) : null;
    });

    // Every plant as a specimen for bro.scene's forest node
    // (scene.createForest): bark thickest first, crown clumps, leaves in a
    // thinning order, one instance per plant where it grew. Swap
    // `instances` (bro.flora.plantForest) to draw the specimens many times.
    fn(FloraWorld.prototype, "emitForest", function emitForest(opts) {
        return _native ? _native.emitForest(this, opts) : null;
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
