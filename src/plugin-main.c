#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("boomboxstream", "en-US")

extern struct obs_source_info boombox_filter_info;

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Audio-reactive bloom and zoom filter (speaker-style pulse).";
}

bool obs_module_load(void)
{
	obs_register_source(&boombox_filter_info);
	blog(LOG_INFO, "[boomboxstream] loaded (version %s)", PLUGIN_VERSION);
	return true;
}
