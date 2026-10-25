# slonk-the-game

<img width="100%" alt="Approaching the ringed gas giant" src="https://github.com/user-attachments/assets/ef7e43e8-1f14-4487-a923-1a5c9cd51bac" />

A flyable, life-size Star Trek solar system in Unreal Engine 5.8. You fly a kinematic Enterprise with impulse and warp drive through a procedurally generated star system at real scale: planets with terrain, atmospheres, clouds and rings, an asteroid belt, spinning space stations, patrolling starships that warp between them, a Borg cube, and combat with shields, phasers, torpedoes and a tractor beam. A floating origin keeps AU-scale distances precise, and an original-series bridge set gives a first-person view.

The project lives in `unreal/` (open `Slonk.uproject` in UE 5.8, build the `SlonkEditor` target, open the `SpaceTest` map and press Play). C++ is in `unreal/Source/Slonk`; materials and the map are generated assets under `unreal/Content/Slonk`.

## Flight

https://github.com/user-attachments/assets/80bc381e-5d8e-4249-b6ed-9dcae36ad8f8

## Planets

Every world is generated at real scale from a seed: Perlin and fBm height and biome noise, Earth-size relief, clipmap terrain that stays sharp down to the surface, volumetric atmospheres and cloud decks, rings from a generated profile, lava emissive and ocean sun glint.

<img width="49%" alt="An Earth-like world from three radii out" src="https://github.com/user-attachments/assets/42d6cd62-72c5-4062-8516-d1e837b71ad9" /> <img width="49%" alt="Low orbit over the Earth-like world: ocean, coast and terrain" src="https://github.com/user-attachments/assets/c009fe6d-9786-43b7-af09-f8cceb31af4e" />
<img width="49%" alt="Night side of the Earth-like world with its terminator" src="https://github.com/user-attachments/assets/5d5dcfbd-dbcd-4764-bb0a-a9618feb1355" /> <img width="49%" alt="An ocean world under cloud" src="https://github.com/user-attachments/assets/cb9d117b-40a4-45d1-a7de-e977e17f2306" />
<img width="49%" alt="Sunrise over the ocean world from low orbit" src="https://github.com/user-attachments/assets/468df791-4dfd-4ce7-9a3e-53fdeefb1297" /> <img width="49%" alt="The ringed gas giant with the ring shadow across it" src="https://github.com/user-attachments/assets/3c48e414-9391-438a-a735-2e00a7918409" />
<img width="49%" alt="A lava world glowing through its crust" src="https://github.com/user-attachments/assets/5ff6c174-db00-460d-ae95-16fe0cceaa0d" /> <img width="49%" alt="The star up close, photosphere and corona" src="https://github.com/user-attachments/assets/3d069559-7eeb-43a1-a4a9-e001e798e566" />

## Gallery

### Ships and stations at night
Every hull has running lights, floodlights on the saucer, pylons and nacelles, and rows of lit windows that show through on the dark side. Stations carry hub and ring windows, a strobe chase and beacons.

<img width="49%" alt="Enterprise backlit by the sun, floodlights on the saucer" src="https://github.com/user-attachments/assets/d28d66e8-8809-415b-a66c-914168c3e37b" /> <img width="49%" alt="Enterprise from the cinematic camera" src="https://github.com/user-attachments/assets/1f9b075a-93bb-4f57-9bda-a464eb4525b8" />
<img width="49%" alt="Station hub and ring windows up close" src="https://github.com/user-attachments/assets/2fd5cd50-4258-4961-9fa9-efedcf7c1cbb" /> <img width="49%" alt="Traffic ship night side with window rows and floodlights" src="https://github.com/user-attachments/assets/b44cbb1b-cc35-4750-85e5-37ebffd8a360" />

Starbase 11 from its dark side, 15 s of real time:

https://github.com/user-attachments/assets/450cdcbb-8be5-46d5-83af-854a7c955e2c

### Going to warp
Two visual modes, switched with `warpvis classic` and `warpvis real`. Classic is the show look: star streaks, a flash and a stretch. Relativistic drops the cartoon streaks and shows aberration, Doppler shift and retarded-time imaging instead.

Chase, cinematic and bridge cameras, classic view:

https://github.com/user-attachments/assets/097a5954-c4b4-4c47-8374-9f43a0fe3827

https://github.com/user-attachments/assets/66308c57-9e66-44d6-b46b-374f546f62cb

https://github.com/user-attachments/assets/5eb14f03-f596-406d-a75e-e2ecba238107

The same jump in the relativistic view, where the star field crowds forward and blue-shifts:

https://github.com/user-attachments/assets/0eb46304-8d26-4cdf-99b2-8ab644f1f65a

https://github.com/user-attachments/assets/e81d4a70-784e-43a0-858e-3f746cb1df78

https://github.com/user-attachments/assets/ae9b49f0-d2b0-4ac6-9900-d70482e4f358

### Other ships warping
In the classic view a ship stretches and leaves a streak when it departs, and the streak whooshes in with the ship at its head when it arrives.

https://github.com/user-attachments/assets/ab35df8d-78da-47e3-b14b-b9e09174cc26

https://github.com/user-attachments/assets/6bd91f94-6756-4c40-98d8-9c3782cfefde

<img width="60%" alt="Classic streak as a traffic ship jumps to warp" src="https://github.com/user-attachments/assets/b09981b0-4568-4d51-a524-acf7a0170d60" />

In the relativistic view a ship moving faster than light is seen as several images. Light from the point of the track nearest you arrives first, and the images then run outward along the track in both directions from that point. The visual light speed is scaled down (`cscale`) so the effects span seconds.

Head-on arrival, real time. The ship warps from 25 km ahead to 2 km ahead; it pops into view at its arrival point first, and an after-image then runs backward up the track, red-shifted and shrinking, until it replaces the old pre-departure image:

https://github.com/user-attachments/assets/858b2e8a-76b2-4eaf-92c0-db17e63479dd

Beside the track, 4x slow motion. A copy appears mid-track and splits in two: one runs back to the departure point and merges with the old image, the other slides on to the destination and becomes the live ship:

https://github.com/user-attachments/assets/ee582208-d1a2-4239-a63b-071e58248faf

<img width="60%" alt="Three images of one ship beside its warp track" src="https://github.com/user-attachments/assets/f341cdab-8d2b-43e2-baf7-50296aa4c914" />

Behind the track, real time. The receding image reddens and shrinks toward the vanishing point:

https://github.com/user-attachments/assets/3271252b-a0df-45c7-9a7a-29f8ed41f3b8

Passing arrival, 2x slow motion. The track passes 0.6 km beside the camera, so the ship sweeps in as a large red-shifted image before settling ahead and turning white:

https://github.com/user-attachments/assets/5e137a1f-8cf0-4ea4-841b-f3e616136961

<img width="60%" alt="Red-shifted receding image of a ship" src="https://github.com/user-attachments/assets/2515ae48-1b77-401d-b575-ad994a6e6c6c" />

## Credits

The Enterprise hull flown in the space test is the one paid asset in the project: "U.S.S. Enterprise NCC-1701-A" (https://skfb.ly/6RsXJ) by Major Stress is licensed under Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/). Everything else is generated by the project's own scripts and code.
