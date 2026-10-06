CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude
LIBS = -lreadline

TARGET = shellforge
SRC = src/main.c src/token.c src/lexer.c src/history.c src/parser.c src/expand.c src/builtin.c src/executor.c src/jobs.c src/job_control.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	bash tests/run_tests.sh

clean:
	rm -f $(TARGET)

rebuild: clean all
