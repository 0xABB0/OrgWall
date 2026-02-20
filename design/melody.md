# Melody engine

## Reasoning:

We need a way to run melody headless or automatically.

This is going to be used by:
- automated testing (aka, running the game and testing against logic bugs)
- automated visual testing (aka, running the game and testing wether there are visual changes)
- something like doom menu screen (seeing a pre-recorded gameplay)
- replays in game like starcraft

Requirements for this:

- if i run the automated tests for logic, the game should not draw. we're only testing logic
- replays should be deterministic. we should have the same result every frame
- running simulations should not take the same time as the recording: we should be able to tick the simulation multiple times per frame (and still be deterministic)
- replaying a simulation means re-inputting the various inputs, and the simulation should not take the current inputs (aka, if i'm in the menu screen with a replay, i don't want to press down and have the character move down.)

Design:

We now have two different concepts:
- something that can tick, receive inputs, and be drawn
- something that automatically ticks, routes input, and draw the main simulation (? not sure. discuss)

## Simulation

Simulation is the game, basically.
A simulation is something that can be updated in real time

## Timeline

The timeline is something in the main loop.
It owns a simulation and automatically routes inputs, and ticks and updates the simulation.
