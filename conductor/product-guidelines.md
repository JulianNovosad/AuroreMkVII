# AuroreMkVII - Product Guidelines

## Documentation Style

### Tone and Voice
- **Technical and Precise:** Use exact terminology, avoid ambiguity
- **Concise:** No filler content; every sentence must add value
- **Professional:** Maintain engineering-grade documentation standards
- **Educational:** Explain complex concepts clearly for skill acquisition

### Document Structure
- Use clear hierarchical headings (H1 → H2 → H3)
- Include tables for specifications and parameters
- Use code blocks for all technical examples
- Add disclaimers where safety/educational context is needed

### Visual Identity
- **Military-Spec Aesthetic:** Clean, monochrome, functional design
- **No Decorative Elements:** Form follows function
- **Readable Typography:** Monospace fonts for technical content
- **High Contrast:** Ensure readability in various lighting conditions

## Code Documentation

### Comment Philosophy
- **Comment "Why", not "What":** Code should be self-explanatory for logic
- **Minimal Inline Comments:** Use only for non-obvious decisions
- **File Headers:** Include purpose, author, date for new files
- **Function Documentation:** Brief description for public APIs

### Naming Conventions
- **Classes:** PascalCase (`SafetyMonitor`, `LockFreeRingBuffer`)
- **Functions/Variables:** snake_case (`update_frame`, `frame_count`)
- **Constants:** kConstantName (`kMaxFrameSize`, `kDefaultTimeout`)
- **Enum Values:** kEnumValue (`kStateBoot`, `kStateTracking`)

### File Organization
- **Headers:** `include/aurore/` with `.hpp` extension
- **Sources:** `src/` with `.cpp` extension
- **Tests:** `tests/` mirroring source structure
- **Scripts:** `scripts/` with `.sh` extension

## UI/UX Guidelines (aurore-link)

### HUD Design Principles
- **Sterile Display:** No decorative elements, pure functionality
- **Monochrome Palette:** White/gray on black, no color coding
- **Edge-Positioned Telemetry:** Keep center clear for reticle
- **Instant Updates:** No smooth animations, mechanical feel
- **Aliased Typography:** Pixel-perfect, no anti-aliasing

### Interaction Design
- **Invisible Controls:** Functional elements hidden from view
- **Keyboard Shortcuts:** Primary input method (when implemented)
- **Touch Support:** Secondary input for mobile/tablet
- **Immediate Feedback:** State changes reflected instantly

## Safety Messaging

### Required Disclaimers
All documentation must include:
> "Educational/personal use only — not for safety-critical deployment."

### Risk Communication
- Clearly state platform limitations (RPi5 not safety-rated)
- Document known issues and failure modes
- Provide mitigation strategies for identified risks

## Version Control

### Commit Messages
- Use Conventional Commits format: `type(scope): description`
- Keep subject line ≤72 characters
- Include body explaining "why" for non-trivial changes
- Reference tests/verification in commit body

### Branch Naming
- `feature/<description>` - New features
- `fix/<description>` - Bug fixes
- `refactor/<description>` - Code restructuring
- `docs/<description>` - Documentation updates
