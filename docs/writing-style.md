# Writing Style Guide

Derived from analysis of how-to documentation, README, and structure documents
across this repository.

---

## 1. Markdown Documentation

### Structure and hierarchy

- `#` for the document title only.
- `##` for top-level sections. `###` for subsections. Never go deeper than `###`.
- Lead every section with 1–2 sentences of purpose before showing code or a table. Do not drop the reader straight into a code block with no context.
- Order sections to follow the reader's mental model: concept → constraint → how to do it → example → edge cases.

### Prose style

- **Direct, declarative sentences.** Subject-verb-object. Prefer active voice.
  - Good: "The framework calls the descriptor callbacks in a fixed order."
  - Avoid: "Callbacks are called by the framework in a fixed order."
- **State consequences, not suggestions.**
  - Good: "If they are different, `connect_port` will bind to the wrong buffer."
  - Avoid: "You should keep them in sync to avoid issues."
- **Constraints use "must".** Permissions use "may". Recommendations use "should". Keep these modal verbs precise.
- **Warnings use the GFM alert block:**
  ```
  > [!WARNING]
  > `run` executes on the audio thread and must be real-time safe.
  > Never allocate memory, call `malloc`/`free`, take a mutex, or do any blocking I/O inside `run`.
  ```
  Use `[!NOTE]` for non-critical callouts, `[!WARNING]` for correctness or safety constraints, `[!CAUTION]` for destructive or irreversible actions.
- **No hedging.** Do not write "might", "could potentially", "it is generally a good idea to".
- **Paragraph length:** 2–4 sentences maximum. One blank line between paragraphs.

### Vocabulary

- **"since"** for causation (not "because" or "as"):
  - "since this is how LV2 discovers functions"
  - "since the same term can be misinterpreted"
- **"instead"** for contrasting with the old or wrong approach.
- **"so that" / "so"** for purpose and consequence.
- **"in addition"** to append a secondary requirement after the primary.
- **Precise technical language over plain English when there is no ambiguity.** Do not say "the buffer pointer" when you can say "the `data` argument to `connect_port`".

### Inline formatting

- Backtick everything that is code: function names, file names, struct fields, port symbols, CLI flags, enum values, URIs, build option names.
- Bold only for critical constraints. Do not use bold for emphasis or to highlight interesting information.
- Use inline hyperlinks (`[text](url)`) for documentation cross-references within prose. Use bare URLs only in commit bodies.
- Block quotes (Markdown `>`) for verbatim external specification text.

### Code examples

- Every concept that has a non-obvious implementation gets a code block.
- Label examples with context before showing the code: "A minimal audio in/out pair:", "Such an `lv2_descriptor()` will typically take on the following form (except when including multiple plugins in a library):"
- Examples are complete — they could compile with minimal additions. Do not show pseudo-code or ellipsis-only skeletons unless explicitly abbreviated.
- "snippet taken from `filename.c`" when lifting directly from source. Keeps examples honest.
- Language-tag all fenced code blocks: ` ```c `, ` ```cpp `, ` ```turtle `, ` ```json `, ` ```meson `, ` ```sh `.
- Inline comments inside examples explain what an individual line does, not the concept (the surrounding prose already did that). Format: `/* brief noun phrase */`.

### Tables

- Used for mappings with 2–4 columns: type matrices, field specs, option lists.
- Every column has a header. Left-align by default.
- Descriptions in table cells are brief noun phrases or fragments, not full sentences.

### Lists

- Numbered lists for ordered sequences (lifecycle order, setup steps).
- Bullet lists for unordered sets (required fields, port types).
- Bullet + em-dash (` — `) for definition-style entries: `- name — identifier for this configuration`.
  - Note: this em-dash style uses a space before and after.
- Do not mix ordered and unordered lists at the same level.

### TODOs and stubs

- Incomplete sections use HTML comment TODOs inline: `<!-- TODO: ... -->`.
- Stub documents (like SETUP.md) are allowed during development; a single sentence placeholder is fine. Do not fill space with filler.

---

## 3. Naming Conventions (as observed in documentation)

- Sections in how-to docs follow the implementation order, not alphabetical order.
- Section titles use sentence case with backticks for code terms: "Declaring ports in the TTL file", "Implementing connect_port".
- Cross-references to code use the exact identifier with backticks, not paraphrase.

---

## 4. Register and Assumed Knowledge

- Audience: engineers familiar with C, Linux systems programming, and audio DSP basics.
- Does **not** over-explain standard C idioms (`calloc`, `pthread_create`, `memset`).
- Does **explain** domain-specific concepts on first use: LV2 plugin lifecycle, PipeWire graph topology, SPSC queues for real-time safety.
- External specs are linked, not summarized. The reader is expected to follow the link for details.
- Technical numbers are always concrete: "~2 s at 48 kHz / 512 samples", "192 blocks", "±0.9926 per audio block".

---

## 5. What This Style Is Not

- No exclamation marks, informal tone, or filler phrases ("Note that...", "Keep in mind...").
- No em-dashes in prose sentences (only in definition-list bullet entries as noted above).
- No "we" in documentation prose.
- No passive hedges: not "it should be noted that", "care should be taken", "it is recommended that".
