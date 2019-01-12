
libpsb-test: $(patsubst %.c,%.o,$(wildcard *.c))
	gcc $^ -o $@ -pthread

%.o: %.c
	gcc -c -MD $<

include $(wildcard *.d)

clean:
	rm *.o *.d
  
