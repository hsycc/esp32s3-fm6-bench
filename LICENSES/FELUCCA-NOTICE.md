# Felucca / MSFA attribution

This benchmark imports a small portion of the Felucca project at the pinned revision:

`5738617968a22b5d4c0d3470cfcc95559052f446`

Upstream project: `hugelton/Felucca`

The imported `firmware/src/fm6_core.c` identifies itself as an integer C port of the MSFA 6-operator FM synthesis core used by Dexed and carries an Apache-2.0 SPDX header. The generated fixed-point tables are produced using Felucca's own generator and its Felucca-Engines submodule.

This repository does not attempt to relicense upstream material. When redistributing imported/generated upstream files, retain all applicable upstream copyright, SPDX and license notices and review Felucca's current licensing documentation.

The benchmark harness code in this repository is separate from the imported upstream material.
