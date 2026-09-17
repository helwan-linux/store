CC = gcc
CFLAGS = `pkg-config --cflags gtk+-3.0` -Iinclude -Wall -O3
LIBS = `pkg-config --libs gtk+-3.0` -lalpm -lpthread

TARGET = hel-store
SRCS = src/main.c src/backend_alpm.c src/ui_window.c src/icon_manager.c src/async_loader.c src/ui_about.c

PREFIX = /usr

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(SRCS) -o $(TARGET) $(CFLAGS) $(LIBS)

install: $(TARGET)
	install -Dm755 $(TARGET) $(PREFIX)/bin/$(TARGET)
	install -Dm644 data/hel-store.png $(PREFIX)/share/icons/hicolor/256x256/apps/hel-store.png
	install -Dm644 data/hel-store.desktop $(PREFIX)/share/applications/hel-store.desktop
	# تثبيت ملف الـ CSS في مسار التوزيعة الخاص بالبرنامج
	install -Dm644 data/style.css $(PREFIX)/share/helwan/hel-store/style.css

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)
	rm -f $(PREFIX)/share/icons/hicolor/256x256/apps/hel-store.png
	rm -f $(PREFIX)/share/applications/hel-store.desktop
	# إزالة ملف الـ CSS ومجلد التوزيعة لو فاضي
	rm -f $(PREFIX)/share/helwan/hel-store/style.css
	rmdir -p $(PREFIX)/share/helwan/hel-store 2>/dev/null || true

clean:
	rm -f $(TARGET)
