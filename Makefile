all:
	@echo "Making deviceconsole..."
	@$(CC) -O3 main.c tail.c -o deviceconsole -F/System/Library/PrivateFrameworks/ -framework MobileDevice -framework CoreFoundation
	@mv ./deviceconsole /usr/local/bin
	@echo "deviceconsole succesfully built and installed. :D"

.PHONY: all
