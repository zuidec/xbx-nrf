import sys


def main():
    if len(sys.argv) > 1:
        from .cli import main as cli  # noqa: PLC0415

        cli()
    else:
        from .gui import main as gui  # noqa: PLC0415

        gui()


if __name__ == "__main__":
    main()
