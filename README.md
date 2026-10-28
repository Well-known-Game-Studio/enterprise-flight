# enterprise-flight

<img width="100%" alt="The highlight run: down a canyon on the Earth-like world at 250 m/s, up through the cloud deck into orbit, warp across the system to the icy moon of the ringed ice giant Caerulea, on into the nebula beyond it, and warp home to the night side of Starbase 11" src="docs/media/highlight.webp" />

Two and a half minutes in one take, flown by the ship's autopilot: the inner gorge of a canyon on the Earth-like world, the climb out through the clouds, a moment in low orbit, warp 9.9 across 2.9 billion km to Caerulea I with its ringed ice giant standing over the lineae, full impulse into the Caerulea Nebula, and warp back to the night side of Starbase 11 with its windows lit.

A flyable, life-size Star Trek solar system in Unreal Engine 5.8. You fly a kinematic Enterprise with impulse and warp drive through a procedurally generated star system at real scale: planets with terrain, atmospheres, clouds and rings, an asteroid belt, spinning space stations, patrolling starships that warp between them, a Borg cube, and combat with shields, phasers, torpedoes and a tractor beam. A floating origin keeps AU-scale distances precise, and the Star Trek VI bridge set gives a first-person view from the captain's chair.

The project lives in `unreal/` (open `Slonk.uproject` in UE 5.8, build the `SlonkEditor` target, open the `SpaceTest` map and press Play; the Unreal project and module keep their original working name, Slonk). C++ is in `unreal/Source/Slonk`; materials and the map are generated assets under `unreal/Content/Slonk`. The clips below are animated WebP files in `docs/media`, captured in play.

## Scale

The system is real size. Planets are thousands of kilometres across, orbits are tenths of an AU to tens of AU apart, and the ship is 305 m long, so nothing is faked with a skybox: the planet behind the station below really is 23,000 km away. Full impulse is a quarter of the speed of light, with a throttle curve that still gives km/s precision near a station.

<img width="100%" alt="Full impulse at the ringed ice giant Caerulea: the planet grows from a point to fill the view, and the ship brakes over its rings" src="docs/media/approach_caerulea.webp" />

<img width="100%" alt="Leaving Starbase 11 looking aft: a slow drift past the station, then full impulse, and the planet behind it shrinks to a coin in ten seconds" src="docs/media/leaving_orbit.webp" />

Looking aft from beside Starbase 11: a slow drift past the station, then full impulse. The station is gone in a blink and the Earth-like world behind it shrinks to a coin in ten seconds.

## Impulse and warp

Warp speed follows the show's curve (warp 1 is light speed, warp 3 is 39 c), so a hop between orbits takes seconds and crossing the system takes a minute. Two warp looks are built in, switched with `warpvis classic` and `warpvis real`: the classic streak-and-flash, and a relativistic view with aberration, Doppler shift and retarded-time imaging.

<img width="100%" alt="Going to warp from the chase camera in the classic view: streaks, flash and the stretch" src="docs/media/warp_jump.webp" />

## Planets

Every world is generated at real scale from a seed. The height field is computed in double precision on the CPU in two stages, the way Outerra layers fractal detail over its elevation data: a structure stage (domain-warped continents, ridged mountain ranges, canyons cut into uplifted tablelands with fluted, stratified walls and a river down the inner gorge, craters on the airless worlds) down to 200 m, and a refinement stage that synthesises what lies below it from the structure's local slope (fractured rock on cliffs, talus and boulders beneath them, hummocky soil on the flats) down to 25 cm. Both are deterministic functions of position, so nothing is stored and every visit is the same place. The surface is drawn through a cube-sphere quadtree of mesh chunks that splits toward the camera from six root chunks in orbit down to those 25 cm cells on the ground, so the same surface serves the view from space and a flight along a canyon floor. The sea is part of the same mesh at the water level: within a few kilometres its vertices ride a swell of four Gerstner waves (real geometry, so the horizon and the ship's shadow move with it), and on top of that it is shaded with baked wind waves at two scales that scroll with time and fade out by twenty kilometres so the sun glint from orbit stays sharp, sand showing through the first metres, foam along the shore and on the crests. Atmospheres are volumetric with real scale heights, and near a planet its clouds are a volumetric layer whose height, thickness, density, continuity and colour come from the world's style (cumulus from 2 to 10 km on an Earth-like world, the sulphuric deck from 48 to 70 km on a Venus, thin ice wisps on a Mars, a continuous haze on a Titan-like moon) and that throws its shadows across the ground; the textured cloud shell, now at the top of that layer, takes over from orbit; planets turn at real day lengths, relief is in real proportion, and the gas giants keep a shader-shaded sphere with cloud bands and rings. Orbit hold keeps a chosen altitude while you look around; `hover <m>` follows the terrain (`hover <m> canyon` also steers along the canyon), and `terrain goto river 200 along` parks the ship over the river in the deepest canyon of the nearest world, nose along it.

<img width="100%" alt="Low orbit over the Earth-like world near the terminator: continents, cloud decks and the limb" src="docs/media/low_orbit.webp" />

<img width="100%" alt="Terrain-following at 150 m down the inner gorge of a canyon on the Earth-like world: a meandering river between green banks, a stair of benches and cliffs in shadow on one side, the sunlit wall on the other" src="docs/media/canyon_flight.webp" />

Down the inner gorge of a canyon on the Earth-like world at 250 m/s, 150 m above the floor. The walls are a stack of strata of hashed thickness and hardness: hard layers stand as cliffs, soft ones slump into scree, and each bench wanders along the canyon on its own. Side canyons cut into the walls every few kilometres from a second, denser line field held to the ones that run into the trunk rather than along it; each reaches a varying distance into the plateau and ends in an amphitheatre where its floor climbs back through the strata. The ground is dressed with four tileable texture sets (bedded cliff, talus, desert soil, scrub) baked in Blender from procedural shaders on a 4-D torus so they wrap, packed to colour + height and normal + occlusion + roughness, and chosen per pixel by slope, rock and moisture. They are laid down in 20 m hashed cells of a planet-local lattice, each with its own heading in a frame tied to the ground and projected triplanar within it, so 5 m tiles never line up into a quilt and the beds stay horizontal on the cliffs; the sets modulate the procedural colour rather than replace it. `tools/planet_materials/bake_terrain_textures.py` bakes them and `import_terrain_textures.py` imports them headless.

<img width="100%" alt="The canyon from 1.5 km over the rim: side canyons branching off the trunk, their stepped walls and amphitheatre heads" src="docs/media/side_canyons.webp" />

<img width="100%" alt="Along a shore of the Earth-like world at 60 m: the swell, the surf line and the sand through the shallows" src="docs/media/coast.webp" />

<img width="100%" alt="Up through the cloud deck from the canyon rim: into and over the cumulus, the volumetric layer that throws its shadows on the ground" src="docs/media/clouds.webp" />

<img width="49%" alt="An Earth-like world from three radii out" src="docs/media/still_earthlike.jpg" /> <img width="49%" alt="The Earth-like world along its terminator from two radii out" src="docs/media/still_nightside.jpg" />
<img width="49%" alt="A Venus from 80 km up: the top of the sulphuric cloud deck" src="docs/media/still_venus.jpg" /> <img width="49%" alt="A Mars from 25 km up: dunes, rock and a thin pink sky" src="docs/media/still_mars.jpg" />
<img width="49%" alt="Sunrise over the ocean world from low orbit" src="docs/media/still_ocean.jpg" /> <img width="49%" alt="A lava world glowing through its crust" src="docs/media/still_lava.jpg" />

## Moons

Every ringed giant has an icy moon the size of Europa just outside its rings and a hazy moon the size of Titan further out. The icy moons carry lineae, long double-ridged cracks in the crust stained brown along their floors; the hazy moons keep an orange photochemical haze from 30 to 90 km that turns the sky from below. `terrain goto linea 300 giant` lands on a crack facing the giant.

<img width="100%" alt="Lift-off from the surface of Caerulea I, the icy moon, with the ringed ice giant standing over the lineae" src="docs/media/moon_giant.webp" />

<img width="49%" alt="The hazy moon from 60 km inside its haze: an orange sky" src="docs/media/still_hazymoon.jpg" /> <img width="49%" alt="The lineae of Caerulea I from 100 km" src="docs/media/still_lineae.jpg" />

## Nebulae

A nebula is a sphere of gas the ship can fly into: the material ray-marches it from the camera to the far side or the first thing in the way, through domain-warped noise that thins toward a ragged edge, scattering the star's light and the gas's own glow, and the transmittance left over veils whatever lies behind. One a light-second across lies beyond each ringed giant; the nav list knows them.

<img width="100%" alt="Full impulse into the Caerulea Nebula from two and a half radii out, through the ragged edge into the glow" src="docs/media/nebula_fly.webp" />

<img width="49%" alt="The Planet4 Nebula from three radii out" src="docs/media/still_nebula.jpg" /> <img width="49%" alt="A moon of the giant seen from inside the nebula's edge, its night side lit by the glow" src="docs/media/still_nebula_moon.jpg" />

## Ships and stations

Every station orbits a planet and every planet has traffic: Constitution, Miranda and Excelsior class ships patrol between stations and warp when they leave. Hulls carry running lights, floodlights and rows of lit windows that show through on the dark side; stations have hub and ring windows, a strobe chase and beacons.

<img width="100%" alt="A slow pass four kilometres off Starbase 11, the spacedock turning over the Earth-like world" src="docs/media/starbase_pass.webp" />

<img width="100%" alt="A patrolling ship arrives from warp ahead and to starboard, streak first, ship at its head" src="docs/media/ship_warp_in.webp" />

<img width="49%" alt="Enterprise backlit by the sun, floodlights on the saucer" src="docs/media/still_enterprise.jpg" /> <img width="49%" alt="Starbase 11 on its night side, windows and docking ring lit" src="docs/media/still_station.jpg" />

## Combat

Phasers, photon torpedoes, shields with a visible bubble, hull damage, a tractor beam and a Borg cube that shoots back. Lock the nearest target and fire; the HUD tracks shields and hull.

<img width="100%" alt="Phasers and two torpedoes on the Borg cube from seven kilometres; the cube answers and the shields flare" src="docs/media/borg_combat.webp" />

## The bridge

The Star Trek VI bridge is built from code in Blender (`blender/bridge-st6`) and imported as one mesh with its Okudagram screens bound to live readouts. From the captain's chair the viewscreen shows the real view ahead; the crew figures are placeholders for now.

<img width="100%" alt="From the captain's chair over the ocean world: forward to the helm, a sweep across the starboard stations and back" src="docs/media/bridge_look.webp" />

## Warp, the relativistic view

In the relativistic view a ship moving faster than light is seen as several images. Light from the point of the track nearest you arrives first, and the images then run outward along the track in both directions from that point. The visual light speed is scaled down (`cscale`) so the effects span seconds. Our own jump, chase camera, with the star field crowding forward and blue-shifting:

https://github.com/user-attachments/assets/0eb46304-8d26-4cdf-99b2-8ab644f1f65a

Head-on arrival of another ship, real time. It pops into view at its arrival point first, and an after-image then runs backward up the track, red-shifted and shrinking, until it replaces the old pre-departure image:

https://github.com/user-attachments/assets/858b2e8a-76b2-4eaf-92c0-db17e63479dd

Beside the track, 4x slow motion. A copy appears mid-track and splits in two: one runs back to the departure point and merges with the old image, the other slides on to the destination and becomes the live ship:

https://github.com/user-attachments/assets/ee582208-d1a2-4239-a63b-071e58248faf

<img width="49%" alt="Three images of one ship beside its warp track" src="https://github.com/user-attachments/assets/f341cdab-8d2b-43e2-baf7-50296aa4c914" /> <img width="49%" alt="Red-shifted receding image of a ship" src="https://github.com/user-attachments/assets/2515ae48-1b77-401d-b575-ad994a6e6c6c" />

## Credits

The Enterprise hull flown in the space test is the one paid asset in the project: "U.S.S. Enterprise NCC-1701-A" (https://skfb.ly/6RsXJ) by Major Stress is licensed under Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/). Everything else is generated by the project's own scripts and code.

The Star Trek VI bridge set in `blender/bridge-st6` was modelled from scratch using Donny Versiga's renders of the 2293 Enterprise-A bridge as reference, view for view (https://www.artstation.com/artwork/YeNkG3). None of his files are used; his work set the layout, proportions and look that the set is measured against. Thank you, Donny.
