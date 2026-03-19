The purpose of this guidebook is to lay down principles and guidelines for how to write code and work together on the Melody engine.

# MEL-META: About this guidebook

## MEL-META-000: Guidelines are identified by unique identifiers

Each guideline in this document is identified by a unique identifier (`MEL-META-000`) as well as a name (`Guidelines are identified by unique identifiers`).

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


# MEL-STYLE: Code style

## MEL-STYLE-001: Enums are banned

Enums are non-extendable during runtime. We do not like that.

## MEL-STYLE-002: If a pointer, start stays with the type

In this repo, pointers are considered types. int* x // x's type is "pointer to variable"

## MEL-STYLE-003: misc

Inside .inl files, to help clangd, we include the .h file including it inside, and we use this format:
```c
#ifdef _CLANGD
#pragma once
#include "mat4.h"
#endif
```

We prefer passing descriptors or configurations instead of having a ton of parameters.
In this case, we define the descriptor structure, define the function taking that structure as param with the postfix '_opt' and then define a macro that enables you to call that function like this:


```c
typedef struct {
    // User data passed to Nob_Walk_Entry.data
    void *data;
} Nob_Walk_Dir_Opt;

bool nob_walk_dir_opt(const char *root, Nob_Walk_Func func, Nob_Walk_Dir_Opt);

#define nob_walk_dir(root, func, ...) nob_walk_dir_opt((root), (func), (Nob_Walk_Dir_Opt){__VA_ARGS__})

// then you can call it like this: return nob_walk_dir(param1, param2, .data = children);
```


We never define a MAX_* constant to create an array lazily. if it needs to be dynamic, we make it dynamic.


# MEL

## MEL-X-001: memory

Every allocation should go through the allocator interface that is exposed inside allocator.h or directly through a specific allocator.
There should be no usage of raw malloc/free inside the codebase.
We prefer to use the specific allocator instead of the generic allocator interface, but sometimes we are either forced to, or that piece of code is not performance-critical.
If needed, we can make multiple functions that take different allocators each

## MEL-X-002: compiler

We only support clang as a compiler, and we want to make heavy use of clang specific extensions

## MEL-X-003: visibility

We don't believe in hiding stuff. when things are internal, we prefer to explicitly define functions with a "mel__" (double underscore) prefix (used when it makes sense to expose this kind of functions. example for this is in allocator.arena.h/.inl, we expose the macros to alloc, but for those, we must export also the internal allocation functions)

## MEL-X-004: buffers

When allocating more than one object, we never (unless we have a good reason to) define buffers with a static size.
For example, if we have a list of something, we never use item[MAX\_ITEM\_NUMBER]. we create a stretchy buffer.

## MEL-X-005: ubrella headers

We explicitly don't allow umbrella headers (aka headers that only include other headers)

## MEL-x-005: visibility part 2

We don't want to create indirections. they make the code less clear and more error prone. if we can make something explicit, we do.
We dislike pimpl-style, we dislike "c with classes". Even though sometimes we are required to use them, we prefer not to when possible.

## MEL-X-006: offensive programming

We prefer offensive programming, aka making heavy use of assertions instead of defensive programming (for example checking for null and returning null)

## MEL-X-007: headers guard

We prefer using #pragma once instead of include guards

## MEL-X-008: physical structure

We prefer grouping things based on the functionality, not the type.

Not good:

editor.spritesheet.h
anim.sprite.h

Good:

anim.sprite.h
anim.sprite.editor.h

This extends to all supporting files. Tests, benchmarks, and documentation live alongside the module they belong to, not in separate directories. The full file family for a module:

```
domain.module.h               // main interface
domain.module.fwd.h           // forward declarations
domain.module.cfg.h           // configuration macros
domain.module.inl             // inline implementations
domain.module.c               // main implementation
domain.module.xxx.c           // split implementations / platform variants
domain.module.md              // module documentation (design notes, API contracts, usage)
domain.module.todo.md         // module todo
domain.module.test.spec.c     // tests (nob filters these out of libmelody.a)
domain.module.bench.spec.c    // benchmarks (nob filters these out of libmelody.a)
```

nob uses the `.test.` and `.bench.` segments to filter: these files are never compiled into the library, only collected when running `./nob test` or `./nob bench`.

## MEL-X-009: language

We prefer using only c as our language of choice, though sometimes we are forced to use another language. This should be done sparingly and only when there is no other choice.
Examples:
- Tracy
- Imgui
- Slang integration

## MEL-X-010: lazyness

We do not condone being lazy. If we spot something that's wrong, we either fix it, or we write in the todo file what the problem is

## MEL-X-011: coordinate system

The engine uses a Y-up, right-handed coordinate system for world space. This is the engine's authoritative convention — all math, physics, cameras, and scene graphs assume Y-up.

Each graphics API has its own clip space conventions:
- Vulkan: Y-down, Z [0,1]
- DirectX: Y-up, Z [0,1]
- Metal: Y-up, Z [0,1]
- WebGPU: Y-up, Z [0,1]

Projection matrices (`mel_mat4_perspective`, `mel_mat4_ortho`) are written in Y-up convention. The API backend is responsible for adapting to its native clip space at the viewport level. For Vulkan, this means negative viewport height (`VkViewport { .y = height, .height = -height }`). Other backends do nothing. This keeps the math layer API-agnostic — projection matrices never contain API-specific corrections.

For 2D screen-space rendering (UI, text, overlays), use `mel_mat4_ortho(0, width, height, 0, ...)` — this maps y=0 to the top of the screen (Y-down screen space). For 2D game-space rendering (platformers, physics), use `mel_mat4_ortho(0, width, 0, height, ...)` — this maps y=0 to the bottom (Y-up, consistent with world space).

## MEL-X-012: matrix layout

Matrices are row-major. `Mel_Mat4.rows[0]` is the first row. Matrix multiplication order is `projection * view * model`. This applies to both CPU math and GPU shader data — push constants and buffers send row-major data, shaders consume it as-is via `mul(matrix, vector)`.

## MEL-X-013: handles

Make heavy use of handles. we should limit the number of pointers in this engine. sometimes it makes more sense to use a generic handle and not a generational handle.

