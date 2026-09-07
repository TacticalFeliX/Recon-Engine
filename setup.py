from setuptools import setup, find_packages

setup(
    name="recon-engine",
    version="0.1.0",
    description="High-Speed Financial Reconciliation Engine (Python)",
    packages=find_packages(exclude=["tests*"]),
    python_requires=">=3.10",
    install_requires=["psycopg2-binary>=2.9"],
    entry_points={
        "console_scripts": [
            "reconcile=recon_engine.cli.cli:main",
        ],
    },
)
