# JavaScript Style Guide - AuroreMkVII

## Overview

This document defines the JavaScript coding style for AuroreMkVII's frontend components (aurore-link/). The project uses **ES2022+** features with a focus on simplicity and performance.

## Tooling

### Linting
```bash
cd aurore-link
npm run lint
```

### Formatting
```bash
# If prettier is configured
npx prettier --write .
```

## File Organization

### Structure
```
aurore-link/
├── index.html      # Main HTML file
├── style.css       # Stylesheet
├── main.js         # Application logic
├── package.json    # Dependencies
└── mock-server.js  # Development server
```

## Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| **Variables** | camelCase | `frameCount`, `websocketUrl` |
| **Constants** | UPPER_SNAKE_CASE | `SCENE_WIDTH`, `MAX_RETRIES` |
| **Functions** | camelCase | `updateHUD()`, `connect()` |
| **Classes** | PascalCase | `TelemetryWriter`, `HUDRenderer` |
| **Event Handlers** | on + PascalCase | `onMessage()`, `onConnect()` |

## Formatting Rules

### Indentation
- **Spaces:** 2 spaces per indent level
- **No tabs:** Always use spaces

### Line Length
- **Soft limit:** 100 characters
- **Hard limit:** 120 characters

### Semicolons
- **Always use semicolons:** Explicit termination
```javascript
const value = getValue();  // Not: const value = getValue()
```

### Quotes
- **Single quotes:** For strings
```javascript
const message = 'Hello';  // Not: "Hello"
```

## Code Guidelines

### Variable Declarations
- **`const` by default:** For immutable bindings
- **`let` for reassignment:** When value changes
- **Never `var`:** Function-scoped is error-prone

```javascript
const SCENE_W = 1536;      // Constant
let reconnectDelay = 2000; // Changes over time
```

### Functions
- **Arrow functions:** For callbacks and short functions
- **Named functions:** For module-level declarations

```javascript
// Module-level function
function connect() {
    // ...
}

// Arrow function for callback
ws.addEventListener('message', (ev) => {
    const data = JSON.parse(ev.data);
    render(data);
});
```

### Async Code
- **Promises over callbacks:** Avoid callback hell
- **`async`/`await`:** For sequential async operations
- **Error handling:** Always catch promise rejections

```javascript
async function fetchData(url) {
    try {
        const response = await fetch(url);
        return await response.json();
    } catch (err) {
        console.error('Fetch failed:', err);
        throw err;
    }
}
```

### DOM Manipulation
- **Cache references:** Don't query repeatedly
- **Use `getElementById`:** For single elements
- **Batch updates:** Minimize reflows

```javascript
// Cache at module load
const canvas = document.getElementById('video');
const ctx = canvas.getContext('2d');

// Batch DOM updates
function updateHUD(data) {
    requestAnimationFrame(() => {
        element1.textContent = data.value1;
        element2.textContent = data.value2;
    });
}
```

### Event Handling
- **Remove listeners:** Prevent memory leaks
- **Use `addEventListener`:** Not inline handlers
- **Passive listeners:** For scroll/touch when possible

```javascript
// Passive listener for touch
element.addEventListener('touchmove', (e) => {
    handleTouch(e);
}, { passive: true });
```

## Modern JavaScript Features

### Destructuring
```javascript
const { width, height } = rect;
const [first, ...rest] = items;
```

### Template Literals
```javascript
const message = `Value: ${value.toFixed(2)}`;
```

### Spread/Rest
```javascript
const merged = { ...obj1, ...obj2 };
function sum(...numbers) { }
```

### Optional Chaining
```javascript
const value = data?.nested?.value ?? 'default';
```

### Modules
- **ES Modules:** When supported
- **IIFE pattern:** For browser scripts without modules

```javascript
// Module pattern
'use strict';
(function() {
    // Private scope
    const privateVar = 42;
    
    // Public API
    window.MyModule = {
        publicMethod() { }
    };
})();
```

## Performance Guidelines

### Real-Time Considerations
- **Avoid `console.log`:** In render loops (slow)
- **Pre-allocate arrays:** Don't grow dynamically
- **Cache DOM queries:** Store references
- **Use `requestAnimationFrame`:** For animations

```javascript
// Pre-allocate array
const particles = new Array(100).fill(null).map(() => createParticle());

// Cache DOM
const elements = {
    fps: document.getElementById('fps'),
    status: document.getElementById('status')
};

// Efficient render loop
function render() {
    updateParticles();
    drawParticles();
    requestAnimationFrame(render);
}
```

### Memory Management
- **Clear intervals:** On cleanup
- **Remove event listeners:** When done
- **Null references:** For large objects

```javascript
let interval = null;

function start() {
    interval = setInterval(update, 100);
}

function stop() {
    clearInterval(interval);
    interval = null;
}
```

## Error Handling

### Validation
```javascript
function sendCommand(cmd) {
    if (!cmd || typeof cmd.type !== 'string') {
        console.warn('Invalid command');
        return;
    }
    ws.send(JSON.stringify(cmd));
}
```

### Graceful Degradation
```javascript
try {
    const data = JSON.parse(raw);
    process(data);
} catch (err) {
    console.warn('Parse failed:', err.message);
    // Continue with fallback behavior
}
```

## Security

### Input Sanitization
- **Never use `innerHTML`:** With untrusted data
- **Use `textContent`:** For text insertion
- **Validate WebSocket data:** Before processing

```javascript
// Safe text insertion
element.textContent = userInput;  // Not: element.innerHTML

// Validate WebSocket message
ws.onmessage = (ev) => {
    try {
        const data = JSON.parse(ev.data);
        if (!isValidTelemetry(data)) return;
        render(data);
    } catch {
        // Ignore malformed data
    }
};
```

### CSP Compliance
- **No inline scripts:** Use external files
- **No `eval()`:** Dynamic code execution
- **Nonce for styles:** If required by CSP

## Testing

- **Unit tests:** For pure functions
- **Integration tests:** For WebSocket flow
- **Manual testing:** For UI/UX verification

```javascript
// Simple test helper
function assertEqual(actual, expected, message) {
    if (actual !== expected) {
        throw new Error(`${message}: expected ${expected}, got ${actual}`);
    }
}
```

## Browser Compatibility

### Target Browsers
- **Chrome/Chromium:** ≥100
- **Firefox:** ≥100
- **Safari:** ≥15

### Polyfills
- **None required:** ES2022+ supported on target platforms
- **Feature detection:** For optional features
