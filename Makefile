BUILDDIR=build
CFLAGS=-Wall -g -I./lib
LDFLAGS=

test: build
#	./$(BUILDDIR)/swmeshexp -I obj -O mesh ./cornell_box.obj -o ./cornell_box.mesh
# 	./$(BUILDDIR)/swmeshexp -I mesh -O ply ./cornell_box.mesh -o ./cornell_box.ply
# 	./$(BUILDDIR)/swmeshexp -I obj -O ply ./cube.obj
# 	./$(BUILDDIR)/swmeshexp -I mesh -O ply ./component_window1x1.mesh
#	./$(BUILDDIR)/swmeshexp -I ply -O ply ./window.ply -o ./window_conv.ply		
	./$(BUILDDIR)/swmeshexp -h	

build: build_dir ./$(BUILDDIR)/swmeshexp

.PHONY: ./lib/librply/obj/rply.o
./lib/librply/obj/rply.o:
	$(MAKE) -C lib/librply/ obj

./$(BUILDDIR)/swmeshexp: ./$(BUILDDIR)/main.o ./lib/librply/obj/rply.o
	gcc -g $^ -o ./$(BUILDDIR)/swmeshexp 

./$(BUILDDIR)/main.o: ./src/main.c
	gcc $(CFLAGS) -c $< -o $@

build_dir:
	mkdir -p ./$(BUILDDIR)