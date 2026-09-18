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

    fn(ns_flora, "setWind", function setWind(strength, dirX, dirY) {
        if (strength === undefined) throw new TypeError("bro.flora.setWind: strength is required");
        if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.setWind === 'function') {
            globalThis.__bro_native.flora.setWind(strength, dirX === undefined ? 0 : dirX, dirY === undefined ? 0 : dirY);
        } else if (typeof __bro_flora_native !== 'undefined' && typeof __bro_flora_native.setWind === 'function') {
            __bro_flora_native.setWind(strength, dirX === undefined ? 0 : dirX, dirY === undefined ? 0 : dirY);
        }
    });

    fn(ns_flora, "wind", function wind(strength, dirX, dirY) {
        if (strength === undefined) throw new TypeError("bro.flora.wind: strength is required");
        if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.wind === 'function') {
            globalThis.__bro_native.flora.wind(strength, dirX === undefined ? 0 : dirX, dirY === undefined ? 0 : dirY);
        } else if (typeof __bro_flora_native !== 'undefined' && typeof __bro_flora_native.wind === 'function') {
            __bro_flora_native.wind(strength, dirX === undefined ? 0 : dirX, dirY === undefined ? 0 : dirY);
        }
    });

    fn(ns_flora, "setDensity", function setDensity(density) {
        if (density === undefined) throw new TypeError("bro.flora.setDensity: density is required");
        if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.setDensity === 'function') {
            globalThis.__bro_native.flora.setDensity(density);
        } else if (typeof __bro_flora_native !== 'undefined' && typeof __bro_flora_native.setDensity === 'function') {
            __bro_flora_native.setDensity(density);
        }
    });

    fn(ns_flora, "density", function (density) {
        if (density === undefined) throw new TypeError("bro.flora.density: density is required");
        if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.density === 'function') {
            globalThis.__bro_native.flora.density(density);
        } else if (typeof __bro_flora_native !== 'undefined' && typeof __bro_flora_native.density === 'function') {
            __bro_flora_native.density(density);
        }
    });

    fn(ns_flora, "update", function update(dt) {
        if (dt === undefined) throw new TypeError("bro.flora.update: dt is required");
        if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.update === 'function') {
            globalThis.__bro_native.flora.update(dt);
        } else if (typeof __bro_flora_native !== 'undefined' && typeof __bro_flora_native.update === 'function') {
            __bro_flora_native.update(dt);
        }
    });

    fn(ns_flora, "clear", function clear() {
        if (typeof globalThis.__bro_native !== 'undefined' && globalThis.__bro_native.flora && typeof globalThis.__bro_native.flora.clear === 'function') {
            globalThis.__bro_native.flora.clear();
        } else if (typeof __bro_flora_native !== 'undefined' && typeof __bro_flora_native.clear === 'function') {
            __bro_flora_native.clear();
        }
    });

    fn(ns_flora, "placement", function placement() {});
    fn(ns_flora, "addPlacement", function addPlacement() {});
    fn(ns_flora, "batches", function batches() { return []; });
    fn(ns_flora, "getBatches", function getBatches() { return []; });

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
