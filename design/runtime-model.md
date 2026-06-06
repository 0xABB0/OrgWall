# Runtime Model

melody needs to provide the tools to make an application built with it behave in the most optimized way for the application's use case.

Some examples of use cases:

- A game needs to have a tick function called without waits
- A GUI application needs to react to events sent by the operating system

The first block of this system is the Reactor.

The reactor is the abstraction for this.

struct Mel_Reactor {
    Mel_Source* sources;
    size_t sources_count;
};
