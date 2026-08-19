#!/usr/bin/env python3
"""Generates Dede's factory presets.

Each file is a complete APVTS state dump (root tag "Parameters", one PARAM
node per parameter carrying its un-normalised value), which is the format
fxme::PresetManager reads. Every parameter is written in every file on
purpose: replaceState() swaps the whole tree, so a preset that omitted one
would leave whatever the previous preset had set.

Keep the defaults here in step with createParameterLayout() in
Source/PluginProcessor.cpp.
"""

import os

MAX_REGIONS = 8
MIN_F, MAX_F = 20.0, 20000.0

GLOBAL_DEFAULTS = {
    "dryLevel": 0.0, "wetLevel": 0.0, "outLevel": 0.0,
    "revRoom": 0.7, "revDamp": 0.5, "revWidth": 1.0, "revLevel": 0.0,
}

def band_edge(edge):
    return MIN_F * (MAX_F / MIN_F) ** (edge / MAX_REGIONS)

def region_defaults(slot):
    return {
        "on": 0.0,
        "flo": band_edge(slot), "fhi": band_edge(slot + 1),
        "gate": -100.0, "gain": 0.0, "pan": 0.0,
        "timel": 0.25, "timer": 0.375, "mode": 0.0,
        "fbl": -12.0, "fbr": -12.0, "fbx": -60.0, "send": -60.0,
    }

def fmt(v):
    return ("%.6f" % v).rstrip("0").rstrip(".") or "0"

def build(name, globals_over=None, regions_over=None):
    values = dict(GLOBAL_DEFAULTS)
    values.update(globals_over or {})

    params = [(k, values[k]) for k in GLOBAL_DEFAULTS]

    for slot in range(MAX_REGIONS):
        r = region_defaults(slot)
        r.update((regions_over or {}).get(slot, {}))
        params += [("rg%d_%s" % (slot, k), v) for k, v in r.items()]

    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             '',
             '<Parameters presetName="%s">' % name]
    lines += ['  <PARAM id="%s" value="%s"/>' % (pid, fmt(val)) for pid, val in params]
    lines += ['</Parameters>', '']
    return "\n".join(lines)


# A region that is on, with the awkward defaults (a shut-off feedback, a shut-off
# send) already spelled out so each preset below only states what it means.
def region(lo, hi, **over):
    r = {"on": 1.0, "flo": lo, "fhi": hi}
    r.update(over)
    return r


PRESETS = {
    # Everything at its default: eight unused slots and a clean pass-through.
    # The starting point for building a patch from nothing.
    "Init": ({}, {}),

    # Low, middle and high each on their own tempo-locked delay, spread across
    # the image and getting shorter and quieter as they go up.
    "Three Bands": (
        {"wetLevel": -2.0, "revLevel": -6.0, "revDamp": 0.55},
        {
            0: region(20, 200, mode=1, timel=0.5, timer=0.5,
                      fbl=-14, fbr=-14, send=-18),
            1: region(200, 2000, mode=1, timel=0.25, timer=0.375, gain=-2, pan=-0.5,
                      fbl=-10, fbr=-10, fbx=-18, send=-12),
            2: region(2000, 20000, mode=1, timel=0.1875, timer=0.125, gain=-4, pan=0.5,
                      fbl=-8, fbr=-8, fbx=-12, send=-8),
        }),

    # Cross feedback only, and equal times: everything that goes in comes back
    # alternating between the speakers. Highs only, so the low end stays dry.
    "Ping Pong Highs": (
        {"wetLevel": -1.0, "revLevel": -12.0},
        {
            0: region(1500, 18000, mode=1, timel=0.25, timer=0.25,
                      fbl=-60, fbr=-60, fbx=-3, send=-20),
        }),

    # The gate set well up into the trace, so only the peaks of the band feed
    # the delay and the reverb: a long wash built from transients alone.
    "Gated Wash": (
        {"dryLevel": -3.0, "wetLevel": -6.0, "revLevel": 0.0,
         "revRoom": 0.92, "revDamp": 0.35},
        {
            0: region(100, 12000, gate=-55, gain=3, mode=1, timel=0.75, timer=1.0,
                      fbl=-6, fbr=-6, fbx=-14, send=-3),
        }),

    # Delay times locked to the last MIDI note: at these coefficients the lines
    # are one and two cycles long, so they ring in tune with whatever is played.
    # Send some notes in and the bands become strings.
    "Tuned Strings": (
        {"wetLevel": -3.0, "revLevel": -10.0, "revRoom": 0.6},
        {
            0: region(80, 800, mode=2, timel=1.0, timer=1.005, pan=-0.3,
                      fbl=-1.5, fbr=-1.5, fbx=-20, send=-20),
            1: region(800, 8000, mode=2, timel=0.5, timer=0.502, gain=-3, pan=0.3,
                      fbl=-2.5, fbr=-2.5, fbx=-20, send=-16),
        }),
}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.environ.get("DEDE_ASSETS", os.path.join(here, "Assets"))
    os.makedirs(out, exist_ok=True)

    for name, (g, r) in PRESETS.items():
        path = os.path.join(out, name.replace(" ", "_") + ".xml")
        with open(path, "w") as f:
            f.write(build(name, g, r))
        print("wrote", path)


if __name__ == "__main__":
    main()
