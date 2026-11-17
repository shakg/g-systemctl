# AGENTS.md

Guidance for coding agents working in this repository. Project layout, build commands and architecture are in `CLAUDE.md`.

## Branches

- `main` - released code. Pushes to `main` build release artifacts, and site changes deploy from it. Never commit to it directly.
- `dev` - integration branch. All work lands here first. `main` is updated only by merging `dev` into it.
- Feature branches - short-lived, one per change, cut from `dev` and merged back into `dev`.

## Feature branch workflow

1. Start from an up-to-date `dev`:
   ```bash
   git fetch origin dev
   git checkout -b feat/<short-name> origin/dev
   ```
   Prefixes: `feat/`, `fix/`, `docs/`, `chore/`.
2. Keep each branch to one change, small enough to review in one sitting. Aim to merge within a day or two.
3. If `dev` moves while you work, merge it in (`git fetch origin dev && git merge origin/dev`) rather than letting the branch drift.
4. Open a PR into `dev`, never into `main`. Build and run the tests before asking for review.
5. Delete the branch after it merges, both locally and on the remote.

## Releasing

When `dev` is stable, open a PR from `dev` into `main`. Tag the release (`vX.Y.Z`) on `main` after the merge.

## CI note

`test.yml` and `podman.yml` run their PR checks only for PRs into `main`. Until they also target `dev`, run the tests locally before merging into `dev`.
