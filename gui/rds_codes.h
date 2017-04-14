#include <stdint.h>	/* For typed ints */

int rds_codes_get_ctry_idx(const char *ctry_name);
const char* rds_codes_get_ctry_name(int idx);
int rds_codes_get_ctry_code_by_ctry_idx(int ctry_idx);
int rds_codes_get_ecc_by_ctry_idx(int ctry_idx);
int rds_codes_get_ctry_idx_from_ctry_codes(uint8_t ctry_code, uint8_t ecc);
int rds_codes_get_lang_idx(const char* lang);
const char* rds_codes_get_lang_name(uint8_t lang_idx);
int rds_codes_get_lic_by_lang_idx(uint8_t lang_idx);
int rds_codes_get_lang_idx_from_lic(uint8_t lic);
const char* rds_codes_get_pty_name(uint8_t pty);
