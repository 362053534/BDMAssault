#ifndef _ASSAULT_MC_LOG_H
#define _ASSAULT_MC_LOG_H

/* 将日志追加写入 mc?:POPSTARTER/<filename>，并同时 printf */
void assault_mc_log_init(const char *filename);
void assault_mc_log(const char *format, ...);

#endif
