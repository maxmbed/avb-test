#include <string.h> // Returns the local date/time formatted as 2014-03-19 11:11:52
char* getFormattedTime(void);

// Remove path from filename
#define __SHORT_FILE__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

// Main log macro
#define __LOG__(format, loglevel, ...) printf("%s %-5s [%s] [%s:%d] " format "\n", getFormattedTime(), loglevel, __func__, __SHORT_FILE__, __LINE__, ## __VA_ARGS__)

// Specific log macros with
#define dbg(format, ...) __LOG__(format, "DEBUG", ## __VA_ARGS__)
#define err(format, ...) __LOG__(format, "ERROR", ## __VA_ARGS__)
#define info(format, ...) __LOG__(format, "INFO", ## __VA_ARGS__)
