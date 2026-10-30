# Contributing to g-systemctl

Thank you for your interest in contributing to g-systemctl, a C++ terminal UI for managing system services. This document outlines some of the best practices and guidelines for contributors.

## Code of Conduct

By participating in this project, you agree to abide by the [Code of Conduct](CODE_OF_CONDUCT.md). Please report any unacceptable behavior to the project maintainer.

## Issues

If you encounter a bug, have a feature request, or want to ask a question, please open an issue on GitHub. Before opening a new issue, please search the existing issues to avoid duplicates.

When creating an issue, please provide as much information as possible, such as:

- A clear and descriptive title
- A detailed description of the problem or feature
- Steps to reproduce the bug or use case for the feature
- Expected and actual behavior
- Any relevant code snippets or screenshots
- Any additional context or information

## Pull Requests

If you want to contribute code or documentation to the project, please follow these steps:

1. Fork the repository and create a new branch from the `main` branch.
2. Write clear and concise commit messages that describe the changes you made.
3. Follow the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) for writing idiomatic and maintainable code.
4. Add tests for your code if applicable, and make sure all tests pass.
5. Ensure your code compiles without warnings using `-Wall -Wextra -Wpedantic`.
6. Build and test your changes using the Makefile:
   ```bash
   make build
   make test
   ```
7. Update the documentation if necessary, and make sure it is consistent with the code.
8. Open a pull request with a descriptive title and a summary of your changes. Link any relevant issues in the description.
9. Wait for a review from the project maintainer or a collaborator. Address any feedback or suggestions they may have.
10. Once your pull request is approved and merged, you can delete your branch.

## CI/CD

This project uses GitHub Actions for continuous integration and release automation.

### Workflows

- **Test** (`.github/workflows/test.yml`): Runs on every push to `main` and pull requests. It builds the project and runs tests on Ubuntu.

- **Build and Release** (`.github/workflows/release.yml`): Builds Linux and macOS packages on every push to `main`. When you push a version tag (e.g., `v1.0.0`), it also creates the GitHub Release and attaches the packages.

### Creating a Release

To create a new release:

```bash
git checkout main
git pull origin main
git tag v1.0.0
git push origin v1.0.0
```

This will trigger the release workflow, which builds binaries for Linux and macOS and creates a GitHub Release with the artifacts attached.

Do not create the release or upload files by hand in the GitHub UI. The install instructions in the README download fixed file names (`g-systemctl-linux-amd64.deb`, `g-systemctl-linux-x86_64.tar.gz`, `g-systemctl.rb`) from the latest release, and only the workflow produces them. The Linux build runs in an `ubuntu:22.04` container so the binary works on older distros; a local build will usually require a newer glibc.
