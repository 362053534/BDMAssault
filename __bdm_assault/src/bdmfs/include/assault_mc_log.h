#ifndef _ASSAULT_MC_LOG_H
#define _ASSAULT_MC_LOG_H

void assault_mc_log_init(const char *filename);
void assault_mc_log(const char *format, ...);
void assault_mc_log_flush(void);

#endif
