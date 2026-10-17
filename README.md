# Chrome Horizon

A Bryce 3D-style landscape maker that runs in the browser, built for phones.

Fractal terrain, chrome and glass objects, water, lava, ice, checkerboard and neon floors, skies with clouds, moons, stars and ringed planets. Everything is raytraced on the GPU in one WebGL 2 shader.

## Run it

It's a single file with no build step. Open `index.html` in a browser, or serve the folder:

```
python3 -m http.server 8000
```

To host it, turn on GitHub Pages for the repo and point it at the root. The app lives at `index.html`. Here, every push to `main` is copied to `gh-pages` by `.github/workflows/publish.yml`, and Pages serves that.

Needs a browser with WebGL 2 (any recent Safari, Chrome, or Firefox).

Live at https://djessemann.github.io/chrome-horizon/

## Put it on your iPhone or iPad

Open the link in Safari, tap Share, then **Add to Home Screen**. It opens full screen like an app and works offline after the first visit.

- Portrait: picture on top, controls below. On iPad the controls go into two columns.
- Landscape: picture on the left, controls on the right.

## Using it

- Drag the picture to look around. Pinch it to zoom the lens.
- Pick a scene at the top, or tap **Surprise me** for a random one.
- Tabs: **Land**, **Sky**, **Surface**, **Objects**, **Camera**.
- **Render HD** renders at 1280, 1920, or 2560 px. Tap **Save or share** (or press and hold the image) to save it to Photos.
- Your last scene is saved in the browser.

## Files

- `index.html`: the whole app (HTML, CSS, shader, and JavaScript).
- `manifest.webmanifest`, `sw.js`, `icons/`: home screen app and offline support.
- `renderer/bryce.c`: the original command-line raytracer in C that the app was ported from. Build with `gcc -O3 -ffast-math -fopenmp renderer/bryce.c -o bryce -lm`, then run `./bryce <scene 1-4> <width> <height> <samples> out.ppm`.
- `samples/`: four renders from the C version.

## How it works

- Terrain is a heightfield from layered value noise, with five styles: eroded mountains, ridged peaks, dunes, buttes, and craters.
- Objects are signed distance functions (spheres, rings, cubes, pillars, cones, pyramids, crystals, boulders, capsules, a sculpted head, and a posable figure), raymarched with soft shadows, reflections, and glass refraction.
- The picture renders in horizontal bands so phones don't stall, and fills in from the top down like Bryce did.
