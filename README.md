# CMF HFT Market-Making Backtester

## Directory structure
.
├── notebooks
├── src
├── tests
└── tmp
    ├── data
    │   ├── MD
    │   │   └── __MACOSX
    │   └── MD_small
    └── hws
        └── data
## Test

`python -m unittest $FILE.py`

## Contributing

Install UV, create a virtual environment, and install the project dependencies:

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
uv sync
```

Then activate the virtual environment and set up the git pre-commit hooks:

```bash
source .venv/bin/activate
pre-commit install
```

After that, formatting and linting will run automatically before each commit.
If the source code does not meet the required formatting rules, the hook will
modify the files and stop the commit, and you will need to stage the updated
changes manually.

To run formatting and linting yourself, use one of these commands:

```bash
pre-commit run --files file.py
pre-commit run --all-files
```

The current pre-commit hooks do the following:
- format and lint C++ code with `clang-format`;
- format and lint Python code with `ruff`;
- strip outputs from Jupyter notebooks.
