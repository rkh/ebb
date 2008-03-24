CC = gcc
CFLAGS = -g -Wall

TARGETS = parser_test

all: $(TARGETS)

parser_test: parser_test.c 
	$(CC) $(CFLAGS) -o $@ $^ -I../src ../src/parser.o

check: parser_test 
	-./parser_test

clean:
	rm -f parser_test
	rm -f $(TARGETS)

.PHONY: all clean check
