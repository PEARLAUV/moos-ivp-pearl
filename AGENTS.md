# Agent Notes

- Use Conventional Commits for commit messages when committing from this repo: https://www.conventionalcommits.org/en/v1.0.0/
- Obtain explicit user permission before creating any new MOOS application, including application scaffolding or a new executable/build target. A general request to fix, configure, or refactor a feature is not permission to create an app. Propose configuration-only or existing-app alternatives first.
- When creating `.parm` files, extensively comment every entry: explain its meaning, units or enum/bitmask values, why the value was chosen, resulting behavior, interactions, and safety limitations. Link official documentation and quote it where useful. Clearly distinguish custom-patch behavior from upstream parameters and cite the patch source.
