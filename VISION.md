# Vision

Where toastsaver is going. This file covers direction; how to build and release is in `README.md`, and how to work in the repo is in `AGENTS.md`.

## Where it is today

toastsaver is a set of retro screensavers for a Raspberry Pi, made for a six-year-old. Every picture is drawn by the program itself. A big, friendly picker lets a kid tap a saver to watch it and give it a heart to keep it.

Several savers already teach a little, though nobody planned that. DICE gives a kid time to add the numbers before the answer flies up. VOLCANO shows lava cooling into rock and an island growing. GERMS acts out an immune system. CITY is full of small things worth pointing at. Kids watch these for a long time, and they ask questions about them.

## Where it is going

The long-term idea: **toastsaver grows from a screensaver collection into a small learning app for kids, in the spirit of [GCompris](https://gcompris.net/)**. It stays offline and free, and it runs on a Raspberry Pi.

It grows in two steps:

1. **Learning savers.** These are savers that teach while you watch: a water cycle, a butterfly growing up, a word of the day. They still run when the computer is idle, and they still close at the first touch.
2. **Things to play with.** Some savers get a second mode that you open from the picker, where a kid can poke the simulation. They could make it rain over the water cycle, feed a caterpillar, or give a coding turtle a program to run. The same simulation drives both modes: you watch it as a saver and play with it as an activity.

GCompris is the model because it gets the important parts right. It has lots of small, focused activities, nothing to buy, no accounts, and it works without a network. It is good for young kids because each activity does one thing. toastsaver adds what it is already good at: lively pixel art, simulations that never run the same way twice, and a front page a six-year-old can use alone.

## Principles

These hold for both savers and activities.

- **A kid can use it alone.** Big targets, pictures before words, no reading needed to get around. Anything for grown-ups sits behind the gear.
- **Watching is enough.** Every saver must still be worth watching with no input at all. Being able to play with it is a bonus.
- **Nothing to fail.** No scores to lose, no timers that punish, no "wrong!" buzzers. Mistakes get a silly or gentle answer, like the dice's grumpy rain cloud for a natural 1.
- **Kind content.** Nobody gets hurt. Germs get washed into healthy cells, and zombies get splashed back to normal.
- **Teach by showing.** A label or one short sentence at the right moment beats a paragraph. The water cycle should be clear with the sound off and the words hidden.
- **Small and offline.** No network, no accounts, no ads, and nothing collected. It runs well on a Raspberry Pi 4. Everything is drawn in code, so the program stays one small binary.
- **Grown-ups can tune it.** Settings like `GERM_SPEED` or the word list belong in `toastsaver.conf`. They never go on the kid's screen.

## Roadmap

### Now: learning savers

The first three to build:

- **WATER**: a cutaway landscape of sea, hills, a lake and mountains under a moving sun. Water warms and rises as vapor, clouds build and drift, rain and snow fall, and rivers carry the water back to the sea. Short labels (EVAPORATION, CONDENSATION, PRECIPITATION, COLLECTION) appear while each stage happens. It pairs naturally with a day/night cycle.
- **BUTTERFLY**: an egg on a leaf hatches into a caterpillar. The caterpillar eats (the leaves get holes) and grows, hangs as a chrysalis, and comes out as a butterfly that flies off and lays the next egg. A small ring shows the four stages and where the butterfly is in the cycle. Each round picks a different kind of butterfly.
- **WORD**: a word of the day with a pixel-art picture of it, the word spelled out letter by letter, and a short sentence that types itself. The word changes daily, so it can be talked about at dinner. Grown-ups can choose the word list (easy, sight words, a child's own list).

Each needs a good *at rest* moment (for example, a new egg or a new day) so SURPRISE rotation can cut away cleanly.

### Next: more learning savers

These come from the same brainstorm. Each one stands alone, so any of them could come next.

- Counting critters (1 to 10, then simple sums)
- Alphabet parade (A for apple)
- Solar system (to-scale orbits that visit each planet)
- Times-table garden (multiplication as rows of flowers)
- Plant and frog life cycles, using the butterfly's stage ring
- Animal habitats, using the fish and birds
- Sorting races (colored bars sorting themselves)
- A globe showing countries, flags and capitals

### Later: things to play with

This is where toastsaver becomes an app as well as a screensaver.

- **Play mode for savers.** A saver that supports it gets a "play" button on its picker tile. In play mode, input goes to the saver and does not close it. A big, always-visible home button takes the kid back.
- **Coding turtle.** This is the first activity that is not a saver. A turtle follows a short program built from picture blocks (forward, turn, pen color, repeat) and draws as it goes. In saver mode, it writes and runs its own programs. In play mode, the kid snaps blocks together. It is the clearest example of the whole vision: worth watching, and even better to play with.
- **Pokeable simulations.** Make it rain in WATER. Put a germ on the slide in GERMS. Drop a dog into CITY. Choose which dice to throw.
- **A picker that grows.** Tiles get grouped (Watch, Learn, Play) once there are too many for one screen. It must stay usable by a kid who can't read yet.

## What this means for the code

Nothing below has to happen yet. These notes keep today's choices from blocking the later steps.

- **The `Saver` struct is the extension point.** Play mode can be one optional callback, something like `int (*input)(void *s, const SDL_Event *e)`, in the same style as `at_rest` and `keep_clear`. A saver without it simply can't be played with.
- **Keep the simulation apart from the input.** Write savers so that "what happens" is driven by functions that player input could also call later (`spawn_rain(x)`, `feed(leaf)`). Avoid burying events inside timers.
- **Content as data.** Word lists, butterfly species and country facts should sit in plain tables, easy to extend and later to translate. They stay compiled in, with an optional file in `~/.config` for families who want their own lists.
- **Text stays short and in capitals.** The 5×7 pixel font is part of the look. Labels should fit it. Longer sentences, if they are needed, may later justify a second, larger font.
- **Translation.** GCompris works in dozens of languages. Keep kid-facing strings in one place per saver so translating them later is possible.

## Not planned

- Accounts, cloud sync, ads, in-app purchases or any network use.
- Grades, streaks, leaderboards or anything that makes a kid feel behind.
- Replacing a school curriculum. This is for curiosity, not homework.
- Leaving behind the screensaver. Idle-time savers stay the heart of the project.

## Open questions

- Should play mode live in the same binary and picker, or in a sibling app (`toastsaver-play`) that shares the engine?
- Could a turtle program built in play mode later run as a saver, so a kid's drawing shows up when the computer is idle?
- Sound: is there a place for it in activities, given that savers are silent?
- Which age range to aim for as activities are added: stay around six, or grow with the kid?
