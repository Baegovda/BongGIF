import sys

if __name__ == "__main__":
    if "--selftest" in sys.argv:
        from bonggif.selftest import main as selftest_main
        sys.exit(selftest_main())
    from bonggif.main import main
    main()
