# Vision

A "batteries included standard library" for everything you want to do in C. Realize any vision for any app, without blocking you from doing what you want.

This codebase is three things:
- an application engine
- a playground
- a study field

It's not production-ready and probably never will be. It's for learning. "You don't need it" does not apply here — we have fun and code things at the best of our ability. But learning doesn't mean lazy: we never write hacks or crutches.

## What it enables

CLI tools, TUI apps, native GUI apps, servers/daemons, games, plugins, fully GPU-rendered GUI applications.

## Target platforms

Desktop (Windows, Linux, macOS), Mobile (Android, iOS), VR (Desktop, Mobile), Web, Consoles.

## Key features

GPU rendering, HTTP, multi-window, multiple event loops (threaded or single-threaded), multi-display rendering at independent refresh rates.

## Focus

- Performance
- Runtime extendability: games can add features without editing engine code. Same for mods. (This is why we prefer dynamic structures and registry patterns.)


# MEL-COMMAND: The Ten Commandments

## MEL-COMMAND-I: Thou shalt not covet foreign tongues

The chosen language was chosen for a reason. He who wandereth toward other tongues hath weak faith. The temptation of safety, of abstraction, of "modern features" — these are sirens upon the rocks. The faithful writeth in one language, debuggeth with one debugger, and knoweth peace.

## MEL-COMMAND-II: Thou shalt not ignore the voice of thy compiler

The compiler speaketh not to hear its own voice. Its warnings are prophecies of bugs yet to come. He who silenceth warnings silenceth wisdom. He who treateth warnings as errors walketh the righteous path. For every warning dismissed, a bug is born.

## MEL-COMMAND-III: Thou shalt not write what thou hast not read

He who modifieth code he doth not comprehend is a surgeon cutting blind. First cometh reading. Then cometh understanding. Only then may the hand touch the keyboard. The fool rusheth in; the wise studieth first.

## MEL-COMMAND-IV: Thou shalt not build more than is needed

Code is not an asset; it is a burden upon the soul. Every line must be maintained, debugged, and explained unto the next poor soul. The righteous writeth the minimum that solveth the problem. The corpse of dead code hath no place among the living — let git remember what man should forget.

## MEL-COMMAND-V: Thou shalt not hide thy intentions

The implicit is the enemy of understanding. Let there be no magic. Let there be no hidden paths. If memory is taken, let it be seen. If state is changed, let it be known. The clever hide complexity; the wise have none to hide. He who readeth shall know what the code doeth, for the code shall tell him plainly.

## MEL-COMMAND-VI: Thou shalt not treat thine errors as enemies

He who curseth the error message curseth the messenger. Errors are not foes; they are teachers bearing difficult truths. The wise welcometh them, for they illuminate the path to correctness. The fool hideth them, silenceth them, ignoreth them — and is visited by segfaults in the night. An error caught is a bug prevented. An error ignored is a demon invited.

## MEL-COMMAND-VII: Thou shalt not serve ghosts nor phantoms

The past self is dead; mourn him not with backwards compatibility. The future self is unborn; burden him not with premature abstraction. Code for the self that existeth now, solving the problem that existeth today. Yesterday's requirements are ghosts; tomorrow's requirements are phantoms. He who buildeth for hypotheticals buildeth houses no one shall inhabit.

## MEL-COMMAND-VIII: Thou shalt not cling to thy creations

Code is not precious, for the world changeth, and code must change with it. He who clingeth to what he hath written maketh an idol of his own labor. When requirements shift, shift with them — not with bitterness, but with acceptance. Every change is a challenge, not an insult. Every rewrite is an opportunity, not a defeat. Delete freely. Refactor gladly. The sunk cost is sunk; let it sink.

## MEL-COMMAND-IX: Thou shalt not stray from the path

When patterns exist, follow them. When conventions are established, honor them. He who introduceth novelty where tradition serveth soweth confusion for all who follow. Consistency is worth more than personal preference. "I like it better this way" is not sufficient cause for deviation. The codebase speaketh with one voice, or it speaketh not at all.

## MEL-COMMAND-X: Thou shalt not forget to live

The code shall wait; thy life shall not. He who liveth only in the terminal hath forgotten why he codeth at all. Go outside. Feel the sun upon thy face. Touch grass beneath thy feet. And if fortune and consent smile upon thee, touch boobies also. The flesh is not merely a vessel for the mind — it hath joys of its own, and they are good. Laugh with friends. Be foolish. Be horny. For what doth it profit a man to mass GitHub stars if he forgetteth the warmth of human touch? The bugs are eternal; thou art not. Live first. Code second.


# MEL-ENGINE: The Ten Commandments of the Engine

## MEL-ENGINE-I: Thou shalt not shy from the hard problem

The engine shall embrace every feature the domain demandeth. Where a capability existeth — in hardware, in protocol, in platform — the engine shall not pretend it doth not exist. To omit a feature out of laziness is cowardice; to defer it out of strategy is wisdom. But to declare "we shall never support this" is forbidden, for the engine that refuseth to grow hath already begun to die.

## MEL-ENGINE-II: Thou shalt hide thy complexity, not thy power

The engine shall bear the burden of complexity so that the user need not. Snake shall be written in 150 lines. A chat application in 300. Yet beneath those few lines liveth the same machinery that rendereth millions of triangles and manageth thousands of connections. The simple path and the powerful path are the same path — the user merely walketh further along it.

## MEL-ENGINE-III: Thou shalt not steal what is not thine

No cycle shall be spent that the user did not ask for. No memory shall be claimed in secret. No thread shall be spawned in shadow. Every cost must be visible, every resource traceable to the hand that requested it. The engine is a steward, not a thief. He who runneth on a phone battery answereth for every wasted watt.

## MEL-ENGINE-IV: Thou shalt constrain conventions, never capabilities

The engine may have opinions — Y pointeth up, matrices are row-major, handles are preferred over pointers. These are conventions, and conventions bring peace. But the engine shall never say "thou canst not." If the user's vision demandeth something the engine did not foresee, the architecture shall bend, not break. Melody constraineth how things are named, never what things are possible.

## MEL-ENGINE-V: Thou shalt not disrespect he who chooseth thee

The user of this engine hath chosen Melody above all others. Honor that choice. Respect his mind — burden it not with unnecessary concepts, cryptic errors, or architectures that require a PhD to navigate. Respect his time — let him not spend days on what should take hours, nor hours on what should take minutes. Respect his product — the engine existeth to serve the user's vision, not to impose its own.

## MEL-ENGINE-VI: Thou shalt respect every device upon which thou runnest

The engine shall be fast not for vanity, but for respect. A phone hath a battery; honor it. A laptop hath a fan; let it rest. An old GPU hath limits; work within them. The web hath constraints; embrace them. Performance is not about speed on the fastest hardware — it is about dignity on the weakest.

## MEL-ENGINE-VII: Thou shalt age forward, not backward

The engine shall build upon what is modern and degrade gracefully unto what is old. It shall not anchor itself to the lowest common denominator, for that is how ambition dieth. When a platform lacketh a capability, the engine shall provide the best that platform can offer — not a broken shadow of the intended experience, but an honest alternative.

## MEL-ENGINE-VIII: Thou shalt fail with honor

When the engine faileth — and fail it shall — it shall do so loudly, immediately, and with full account of what went wrong. No silent corruption. No deferred consequences. In debug, the engine is merciless: every contract violated is an assertion fired, every misuse a crash with a stack trace. The user shall always know what happened and why.

## MEL-ENGINE-IX: TODO

TODO

## MEL-ENGINE-X: Thou shalt remember thy place

Nobody careth about the engine. Not the user. Not the player. Not the investor. Not the journalist. Not thy user's mother, who shall look upon the screen and say "that's nice, dear" — and never, not once, not ever, shall she ask what the framerate is, what the draw calls are, nor whether the matrices be row-major or column-major. Nobody hath ever fallen in love with an engine. Nobody hath ever wept at a render pipeline. Nobody hath ever mass nutted to a descriptor set layout. They weep at the game. They love the app. They remember the experience. The engine is forgotten, and that is the highest honor. Melody existeth not for herself, but for the thing that is built upon her. She is the wingman, not the main character. Her purpose is to make the user's vision so good, so effortless, so undeniably his, that it getteth him mass followers, mass money, mass recognition, and if fortune and consent smile upon him, mass bitches also. And if building with Melody is not fun — if the act of creation bringeth no joy — then we have failed more completely than any segfault could express. Be invisible. Be the reason someone shipped. Then shut up about it.
