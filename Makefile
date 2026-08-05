CC = gcc
CFLAGS = -Wall -Wextra -Iinclude
LIBS = -lreadline

TARGET = shellforge
SRC = src/main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

rebuild: clean allCC = gcc
CFLAGS = -Wall -Wextra -Iinclude
LIBS = -lreadline

TARGET = shellforge
SRC = src/main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

rebuild: clean all
