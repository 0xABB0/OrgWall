# Design Review Checklist

When reviewing or writing a design doc, interrogate it with these questions. Every "no" or "unclear" is a gap that will bite during implementation.

**Is it defined?**
If a type appears in any function signature, it must have a concrete definition in the doc. No "TBD", no hand-waving. This includes internal interfaces — vtables, callbacks, and backend contracts need the same rigor as public APIs. If you can't define it yet, that's a signal the design isn't ready.

**Who owns it? For how long?**
Every pointer that crosses a boundary (async submission, callback, thread handoff, stored reference) needs explicit ownership and lifetime. "Caller-owned" vs "callee-owned" vs "transient until X" — write it down. If one buffer in a system has a different ownership model than all the others, that's a red flag.

**What's the capacity?**
Every writable buffer needs an accompanying capacity parameter. No exceptions. If the callee writes into caller memory, the callee must know the buffer size. Check function signatures for any writable pointer without a corresponding size — each one is a latent overflow or a "where does the output go?" mystery.

**Is it consistent?**
Same concept = same type, same pattern, same naming, everywhere in the API. Scan for the same logical thing appearing with different types (const vs non-const, different integer widths, different naming conventions). Inconsistency is always a bug or a missing design decision.

**What if it partially succeeds?**
Any operation that accepts a batch, a count, or multiple items: what happens when some succeed and others don't? Which ones? In what order? Are failures atomic per group (all-or-none) or prefix-based (first N)? Does a partial failure leave the system in a well-defined state? Silence here means undefined behavior during implementation.

**How do the controls interact?**
If multiple parameters influence the same behavior (e.g. priority + QoS class + deadline all affecting scheduling), define the precedence. Which one wins? Do they compose? Can one override another? Every independent "knob" that touches the same system needs an interaction rule, or the implementer will invent one under pressure.

**Who can call this, and from which thread?**
Every public function needs a thread-safety statement. Can it be called concurrently? From any thread? Only from the thread that created the object? What happens if two threads call it at the same time? Silence means "probably not thread-safe but someone will try it anyway."

**How does it fail?**
Every operation needs defined behavior for: success, failure, partial success, timeout, and resource exhaustion. If a failure is recoverable, define what state is preserved and whether retry is safe. If a buffer is too small, does the operation consume the input or is it retry-safe? Every error code needs to answer: "what do I do now?"

**What happens when I destroy something that's still in use?**
Every close/destroy/unmount/free/remove operation: what if there are live references? Assert and crash? Defer cleanup? Refcount? This is where use-after-free and dangling pointer bugs are born. Pick a strategy and state it explicitly.

**What are the string rules?**
Any system that processes paths, names, or text: define normalization (what transformations are applied?), encoding (UTF-8? null-terminated?), case sensitivity (at which layer?), and separator conventions. These must be documented once and referenced everywhere. Half of all path-related bugs come from unstated normalization assumptions.
