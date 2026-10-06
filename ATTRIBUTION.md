# Forge Attribution in Your Game

Full terms: [LICENSE](LICENSE). This guide provides notices and practical placement rules for game developers.

Standard Core: **“Uses the Forge engine”**.

Your own Core modifications: **“Built on the Forge engine (modified core)”**.

Display the appropriate notice **in both locations**:

1. On the loading screen when the game starts. If none exists, add a startup screen with attribution.
2. In the main menu or an About / About the Engine section accessible directly from the menu.

The notice must be readable, sufficiently contrasted, and visible long enough to read. It may match the game's design and be translated without changing its meaning. A log, README, credits, or store-page notice alone does not replace the two locations.

Shader, physics, graphics-module, and game-content changes alone do not require modified-core attribution. The exhaustive Core list is in [CORE.md](CORE.md) and LICENSE.

Place text in each loading/menu scene through the UI module:

```python
import ui

# In each of the two relevant scenes.
ui.text('engine-attribution', 'Uses the Forge engine', 40, 40, 20)

# Instead, if the Core was modified:
# ui.text('engine-attribution',
#         'Built on the Forge engine (modified core)', 40, 40, 20)
```

This demonstrates text placement, not a complete loading-screen/menu system. Check actual visibility in your layout at all supported window sizes.

Preserve LICENSE and NOTICE in your distribution. `build` copies Forge license documents from the engine installation; `init` copies them into a new game project. For Modified Core, include a change description. Commercial and closed-source games are allowed; their independently created code need not be disclosed.
