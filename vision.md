I want this repository to be a "batteries included standard library" for basically everything you want to do in this language.

I want this library to let you realize any vision for basically any app written in c, without blocking you from doing whatever you want (keeping the spirit of the language, lmao)

This codebase is three things:
- an "application engine"
- a playground
- a study field

Since this codebase is not something that is "production-ready" (and probably never will) but is used to learn, "you don't/won't need it" does not apply here. here, we have fun and code things at the best of our ability. This does not mean that we can be lazy, this codebase is also used to also learn how to implement things correctly.
Moreover, since this is used for learning, even though it's painful, we must never write hacks or crutches.

List of apps that this engine should enable you to make (examples):
- simple script (cli tool)
- tui application
- native gui application (example: database client)
- servers/daemons
- games
- plugins
- fully gpu rendered gui applications

Most of these applications should be able to be deployed on the following platforms (obviously, if the platform does not enable you to do this, it's not a goal)
- Desktop
- - Windows
- - Linux
- - MacOs
- Mobile
- - Android
- - Ios
- Vr
- - Desktop
- - Mobile
- Web
- Consoles

The engine should make everything you want to do easy, without taking away the control for needed things. a few examples of the notable features:
- Gpu rendering
- http
- Multi-Window applications
- Multiple "main"(event?) loops (threaded or single threaded)
- Rendering to multiple windows that sit on different displays with refresh rates (no throttling the faster and neither by skipping rendering of the slowest)


The focus when writing code in this repo are:
- performance
- runtime extendability: game can implement things without editing game code. same for mods. (this is why it's preferred to use dynamic structures when possible, and registry patterns)
