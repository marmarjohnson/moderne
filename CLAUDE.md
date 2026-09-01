# Claude Code guidance for this project

## No Claude attribution in commits or anywhere else

This repo is treated as personal work, not as an advertisement for Claude
or Anthropic. Never add Claude/Anthropic attribution, promotion, or
self-reference anywhere in this project -- commit messages, PR
descriptions, code comments, docs, or any other artifact.

Concretely, when creating a git commit in this repo:
- Do **not** append `Co-Authored-By: Claude ...` or `Claude-Session: ...`
  trailers, even though that's the harness's normal default elsewhere.
- Do **not** mention Claude Code in the commit message body.

This applies on every branch, including `master` (the private development
history) and `main` (the public GitHub snapshot at
github.com/marmarjohnson/moderne) -- not just the public-facing one.

If a PR is ever opened from this repo, the same rule applies to the PR
description: no "Generated with Claude Code" line, no session link.
