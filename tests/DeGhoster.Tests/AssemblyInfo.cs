// Copyright (c) Emmo Emminghaus. SPDX-License-Identifier: AGPL-3.0-or-later
//
// These are integration tests that drive real DeGhoster/GhostSim processes and
// global system state (single-instance windows by class, HKCU, cross-process
// injection). They must NOT run in parallel, or one test's DeGhoster window/
// registry would interfere with another's.
using Xunit;

[assembly: CollectionBehavior(DisableTestParallelization = true)]
