# Working in this repo

Preferences for any agent (or person) developing toastsaver. Build and test details are in `README.md`; this file is about how to work.

## Checkout and branches

- The main checkout, `/Users/ryan/Desktop/files/repos/toastsaver`, belongs to the thread that owns `main` and releases. Don't leave uncommitted work in it.
- Anything longer-lived than a quick fix (a new saver, an enhancement) goes in its own git worktree on its own branch. Never stash, reset or overwrite another thread's changes; message that thread instead.
- Only the thread that owns `main` commits to it and releases. Everyone else merges through that thread, or opens a PR from their branch.

## Before committing

- Validate first. A clean compile is not validation. Run `make check`, and `sh tests/run.sh` when Docker is running. For a saver you touched, also run its snapshot and bench checks.
- Report the results, and give the user the commands to run and eyeball it themselves.
- Ask before committing. Commit only your own files, never `git add -A` on a shared checkout.
- The pre-push hook runs the package tests in Docker. Start Docker rather than skipping the hook with `--no-verify`.

## Releasing

- Follow "Releases" in `README.md`: changelog entry, `VERSION` in `src/engine.h`, the version in `man/toastsaver.1`, commit, then `sh packaging/release.sh --dry-run` and `sh packaging/release.sh`.
- `release.sh` needs a clean tree on `main`. If the shared checkout is dirty with someone else's work, run it from a fresh clone with `origin` pointed at GitHub, then fast-forward the checkout.
- Releases are user-triggered; don't cut one unasked.

## Style of help

- Give absolute paths in every command and file reference, one command per fenced block.
- Keep screensaver content kid-friendly: it is built for the user's young son.
- Hand cheap, self-contained side tasks (history searches, collecting numbers) to a Haiku subagent. Keep saver design and simulation work in the main thread.
- Parked ideas go in the memory "parking lot", not into the code.
