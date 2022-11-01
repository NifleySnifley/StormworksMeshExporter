BUILDDIR=build

test: build
	./$(BUILDDIR)/swmeshexp ./base_01.mesh

build: build_dir ./src/main.c
	gcc -g ./src/main.c -o ./$(BUILDDIR)/swmeshexp

build_dir:
	mkdir -p ./$(BUILDDIR)