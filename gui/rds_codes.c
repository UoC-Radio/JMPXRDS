#include <string.h>	/* For strncmp() */
#include "rds_codes.h"

struct rds_code_map {
	uint16_t	code;
	const char*	name;
};

/*******************\
* RDS COUNTRY CODES *
\*******************/

/*
 * These codes are used to fully determine the station's
 * country, using the first symbol of PI (country code)
 * and the extended country code. The format of the data
 * is <country code>(bits 8-4)<extended country code>
 * (bits 4-1). The values here come from the RDS standard.
 */

/* European Broadcasting Area */
static const struct rds_code_map ctry_codes[] = {
	{ 0x09E0, "Albania" },
	{ 0x02E0, "Algeria" },
	{ 0x03E0, "Andorra" },
	{ 0x0AE4, "Armenia" },
	{ 0x0AE0, "Austria" },
	{ 0x0BE3, "Azerbaijan" },
	{ 0x08E4, "Azores (Portugal)" },
	{ 0x06E0, "Belgium" },
	{ 0x0FE3, "Belarus" },
	{ 0x0FE4, "Bosnia Herzegovina" },
	{ 0x08E1, "Bulgaria" },
	{ 0x0EE2, "Canaries (Spain)" },
	{ 0x0CE3, "Croatia" },
	{ 0x02E1, "Cyprus" },
	{ 0x02E2, "Czech Republic" },
	{ 0x09E1, "Denmark" },
	{ 0x0FE0, "Egypt" },
	{ 0x02E4, "Estonia" },
	{ 0x09E1, "Faroe (Denmark)" },
	{ 0x06E1, "Finland" },
	{ 0x0FE1, "France" },
	{ 0x0CE4, "Georgia" },
	{ 0x0DE0, "Germany (East)" },
	{ 0x01E0, "Germany (West)" },
	{ 0x0AE1, "Gibraltar (United Kingdom)" },
	{ 0x01E1, "Greece" },
	{ 0x0BE0, "Hungary" },
	{ 0x0AE2, "Iceland" },
	{ 0x0BE1, "Iraq" },
	{ 0x02E3, "Ireland" },
	{ 0x04E0, "Israel" },
	{ 0x05E0, "Italy" },
	{ 0x05E1, "Jordan" },
	{ 0x09E3, "Latvia" },
	{ 0x0AE3, "Lebanon" },
	{ 0x0DE1, "Libya" },
	{ 0x09E2, "Liechtenstein" },
	{ 0x0CE2, "Lithuania" },
	{ 0x07E1, "Luxembourg" },
	{ 0x04E3, "Macedonia" },
	{ 0x08E4, "Madeira (Portugal)" },
	{ 0x0CE0, "Malta" },
	{ 0x01E4, "Moldova" },
	{ 0x0BE2, "Monaco" },
	{ 0x01E3, "Montenegro" },
	{ 0x01E2, "Morocco" },
	{ 0x08E3, "Netherlands" },
	{ 0x0FE2, "Norway" },
	{ 0x08E0, "Palestine" },
	{ 0x03E2, "Poland" },
	{ 0x08E4, "Portugal" },
	{ 0x0EE1, "Romania" },
	{ 0x07E0, "Russian Federation" },
	{ 0x03E1, "San Marino" },
	{ 0x0DE2, "Serbia" },
	{ 0x05E2, "Slovakia" },
	{ 0x09E4, "Slovenia" },
	{ 0x0EE2, "Spain" },
	{ 0x0EE3, "Sweden" },
	{ 0x04E1, "Switzerland" },
	{ 0x06E2, "Syrian Arab Republic" },
	{ 0x07E2, "Tunisia" },
	{ 0x03E3, "Turkey" },
	{ 0x06E4, "Ukraine" },
	{ 0x0CE1, "United Kingdom" },
	{ 0x04E2, "Vatican City State" },
	/* African Broadcasting Area */
	{ 0x06D0, "Angola" },
	{ 0x09D1, "Burundi" },
	{ 0x0ED0, "Benin" },
	{ 0x0BD0, "Burkina Faso" },
	{ 0x0BD1, "Botswana" },
	{ 0x01D0, "Cameroon" },
	{ 0x06D1, "Cape Verde" },
	{ 0x02D0, "Central African Republic" },
	{ 0x09D2, "Chad" },
	{ 0x0CD1, "Comoros" },
	{ 0x0BD2, "Democratic Republic of Congo" },
	{ 0x0CD0, "Congo" },
	{ 0x0CD2, "Cote d'Ivoire" },
	{ 0x03D0, "Djibouti" },
	{ 0x07D0, "Equatorial Guinea" },
	{ 0x0FD2, "Eritrea" },
	{ 0x0ED1, "Ethiopia" },
	{ 0x08D0, "Gabon" },
	{ 0x08D1, "Gambia" },
	{ 0x03D1, "Ghana" },
	{ 0x0AD2, "Guinea-Bissau" },
	{ 0x07D0, "Equatorial Guinea" },
	{ 0x09D0, "Republic of Guinea" },
	{ 0x06D2, "Kenya" },
	{ 0x02D1, "Liberia" },
	{ 0x06D3, "Lesotho" },
	{ 0x0AD3, "Mauritius" },
	{ 0x04D0, "Madagascar" },
	{ 0x0FD0, "Malawi" },
	{ 0x05D0, "Mali" },
	{ 0x04D1, "Mauritania" },
	{ 0x03D2, "Mozambique" },
	{ 0x01D1, "Namibia" },
	{ 0x08D2, "Niger" },
	{ 0x0FD1, "Nigeria" },
	{ 0x05D3, "Rwanda" },
	{ 0x05D1, "Sao Tome & Principe" },
	{ 0x08D3, "Seychelles" },
	{ 0x07D1, "Senegal" },
	{ 0x01D2, "Sierra Leone" },
	{ 0x07D2, "Somalia" },
	{ 0x0AD0, "South Africa" },
	{ 0x0CD3, "Sudan" },
	{ 0x05D2, "Swaziland" },
	{ 0x0DD0, "Togo" },
	{ 0x0DD1, "Tanzania" },
	{ 0x04D2, "Uganda" },
	{ 0x03D3, "Western Sahara" },
	{ 0x0ED2, "Zambia" },
	{ 0x02D2, "Zimbabwe" },
	/* ITU Region 2 */
	{ 0x01A2, "Anguilla" },
	{ 0x02A2, "Antigua and Barbuda" },
	{ 0x0AA2, "Argentina" },
	{ 0x03A4, "Aruba" },
	{ 0x0FA2, "Bahamas" },
	{ 0x05A2, "Barbados" },
	{ 0x06A2, "Belize" },
	{ 0x0CA2, "Bermuda" },
	{ 0x01A3, "Bolivia" },
	{ 0x0BA2, "Brazil" },
	/* Canada - Multiple country codes */
	{ 0x0BA1, "Canada" },
	{ 0x0CA1, "Canada" },
	{ 0x0DA1, "Canada" },
	{ 0x0EA1, "Canada" },
	{ 0x07A2, "Cayman Islands" },
	{ 0x0CA3, "Chile" },
	{ 0x02A3, "Colombia" },
	{ 0x08A2, "Costa Rica" },
	{ 0x09A2, "Cuba" },
	{ 0x0AA3, "Dominica" },
	{ 0x0BA3, "Dominican Republic" },
	{ 0x03A2, "Ecuador" },
	{ 0x0CA4, "El Salvador" },
	{ 0x04A2, "Falkland Islands" },
	{ 0x05A3, "French Guiana" },
	{ 0x0FA1, "Greenland" },
	{ 0x0DA3, "Grenada" },
	{ 0x0EA2, "Guadeloupe" },
	{ 0x01A4, "Guatemala" },
	{ 0x0FA3, "Guyana" },
	{ 0x0DA4, "Haiti" },
	{ 0x02A4, "Honduras" },
	{ 0x03A3, "Jamaica" },
	{ 0x04A3, "Martinique" },
	/* Mexico - Multiple country codes */
	{ 0x0BA5, "Mexico" },
	{ 0x0DA5, "Mexico" },
	{ 0x0EA5, "Mexico" },
	{ 0x0FA5, "Mexico" },
	{ 0x05A4, "Montserrat" },
	{ 0x0DA2, "Netherlands Antilles" },
	{ 0x07A3, "Nicaragua" },
	{ 0x09A3, "Panama" },
	{ 0x06A3, "Paraguay" },
	{ 0x07A4, "Peru" },
	{ 0x0AA4, "Saint Kitts" },
	{ 0x0BA4, "Saint Lucia" },
	{ 0x0FA6, "St Pierre and Miquelon" },
	{ 0x0CA5, "Saint Vincent" },
	{ 0x08A4, "Suriname" },
	{ 0x06A4, "Trinidad and Tobago" },
	{ 0x0EA3, "Turks and Caicos Islands" },
	{ 0x09A4, "Uruguay" },
	{ 0x0EA4, "Venezuela" },
	{ 0x0FA5, "Virgin Islands (British)" },
	{ 0x0AF0, "Afghanistan" },
	/* USA including unincorporated territories - Multiple country codes */
	{ 0x01A0, "United States of America" },
	{ 0x02A0, "United States of America" },
	{ 0x03A0, "United States of America" },
	{ 0x04A0, "United States of America" },
	{ 0x05A0, "United States of America" },
	{ 0x06A0, "United States of America" },
	{ 0x07A0, "United States of America" },
	{ 0x08A0, "United States of America" },
	{ 0x09A0, "United States of America" },
	{ 0x0AA0, "United States of America" },
	{ 0x0BA0, "United States of America" },
	{ 0x0DA0, "United States of America" },
	{ 0x0EA0, "United States of America" },
	/* ITU Region 3*/
	{ 0x01F0, "Australia Capital Territory" },
	{ 0x02F0, "New South Wales" },
	{ 0x03F0, "Victoria" },
	{ 0x04F0, "Queensland" },
	{ 0x05F0, "South Australia" },
	{ 0x06F0, "Western Australia" },
	{ 0x07F0, "Tasmania" },
	{ 0x08F0, "Northern Territory" },
	{ 0x03F1, "Bangladesh" },
	{ 0x0EF0, "Bahrain" },
	{ 0x0BF1, "Brunei Darussalam" },
	{ 0x02F1, "Bhutan" },
	{ 0x03F2, "Cambodia" },
	{ 0x0CF0, "China" },
	{ 0x05F1, "Fiji" },
	{ 0x0FF1, "Hong Kong" },
	{ 0x05F2, "India" },
	{ 0x0CF2, "Indonesia" },
	{ 0x08F1, "Iran" },
	{ 0x09F2, "Japan" },
	{ 0x0DE3, "Kazakhstan" },
	{ 0x01F1, "Kiribati" },
	{ 0x0EF1, "Korea (South)" },
	{ 0x0DF0, "Korea (North)" },
	{ 0x01F2, "Kuwait" },
	{ 0x03E4, "Kyrghyzstan" },
	{ 0x01F3, "Laos" },
	{ 0x06F2, "Macao" },
	{ 0x0FF0, "Malaysia" },
	{ 0x0BF2, "Maldives" },
	{ 0x0EF3, "Micronesia" },
	{ 0x0FF3, "Mongolia" },
	{ 0x0BF0, "Myanmar (Burma)" },
	{ 0x0EF2, "Nepal" },
	{ 0x07F1, "Nauru" },
	{ 0x09F1, "New Zealand" },
	{ 0x06F1, "Oman" },
	{ 0x04F1, "Pakistan" },
	{ 0x08F2, "Philippines" },
	{ 0x09F3, "Papua New Guinea" },
	{ 0x02F2, "Qatar" },
	{ 0x09F0, "Saudi Arabia" },
	{ 0x0AF1, "Soloman Islands" },
	{ 0x04F2, "Samoa" },
	{ 0x0AF2, "Singapore" },
	{ 0x0CF1, "Sri Lanka" },
	{ 0x0DF1, "Taiwan" },
	{ 0x05E3, "Tajikistan" },
	{ 0x02F3, "Thailand" },
	{ 0x03F3, "Tonga" },
	{ 0x0EE4, "Turkmenistan" },
	{ 0x0DF2, "United Arab Emirates" },
	{ 0x0BE4, "Uzbekistan" },
	{ 0x07F2, "Vietnam" },
	{ 0x0FF2, "Vanuatu" },
	{ 0x0BF3, "Yemen" },
};

/***********************************\
* RDS LANGUAGE IDENTIFICATION CODES *
\***********************************/

/*
 * 8bin LICs as defined on annex J of
 * the RDS standard
 */

static const struct rds_code_map li_codes[] = {
	{ 0x0, "Unknown/NA" },
	{ 0x1, "Albanian" },
	{ 0x2, "Breton" },
	{ 0x3, "Catalan" },
	{ 0x4, "Croatian" },
	{ 0x5, "Welsh" },
	{ 0x6, "Czech" },
	{ 0x7, "Danish" },
	{ 0x8, "German" },
	{ 0x9, "English" },
	{ 0xA, "Spanish" },
	{ 0xB, "Esperando" },
	{ 0xC, "Estonian" },
	{ 0xD, "Basque" },
	{ 0xE, "Faroese" },
	{ 0xF, "French" },
	{ 0x10, "Frisian" },
	{ 0x11, "Irish" },
	{ 0x12, "Gaelic" },
	{ 0x13, "Galician" },
	{ 0x14, "Icelandic" },
	{ 0x15, "Italian" },
	{ 0x16, "Lappish" },
	{ 0x17, "Latin" },
	{ 0x18, "Lativian" },
	{ 0x19, "Lusembourgian" },
	{ 0x1A, "Lithuanian" },
	{ 0x1B, "Hungarian" },
	{ 0x1C, "Malteze" },
	{ 0x1D, "Dutch" },
	{ 0x1E, "Norwegian" },
	{ 0x1F, "Occitan" },
	{ 0x20, "Polish" },
	{ 0x21, "Portuguese" },
	{ 0x22, "Romanian" },
	{ 0x23, "Romansh" },
	{ 0x24, "Serbian" },
	{ 0x25, "Slovak" },
	{ 0x26, "Slovene" },
	{ 0x27, "Finnish" },
	{ 0x28, "Swedish" },
	{ 0x29, "Turkish" },
	{ 0x2A, "Flemish" },
	{ 0x2B, "Walloon" },
	{ 0x7F, "Amharic" },
	{ 0x7E, "Arabic" },
	{ 0x7D, "Armenian" },
	{ 0x7C, "Assamese" },
	{ 0x7B, "Azerbijani" },
	{ 0x7A, "Bambora" },
	{ 0x79, "Belorussian" },
	{ 0x78, "Bengali" },
	{ 0x77, "Bulgarian" },
	{ 0x76, "Burmese" },
	{ 0x75, "Chinese" },
	{ 0x74, "Churash" },
	{ 0x73, "Dari" },
	{ 0x72, "Funali" },
	{ 0x71, "Georgian" },
	{ 0x70, "Greek" },
	{ 0x6F, "Gujurati" },
	{ 0x6E, "Gurani" },
	{ 0x6D, "Hausa" },
	{ 0x6C, "Hebrew" },
	{ 0x6B, "Hindi" },
	{ 0x6A, "Indonesian" },
	{ 0x69, "Japanese" },
	{ 0x68, "Kannada" },
	{ 0x67, "Kazakh" },
	{ 0x66, "Khmer" },
	{ 0x65, "Korean" },
	{ 0x64, "Laotian" },
	{ 0x63, "Macedonian" },
	{ 0x62, "Malagasay" },
	{ 0x61, "Malaysian" },
	{ 0x60, "Moldavian" },
	{ 0x5F, "Marathi" },
	{ 0x5E, "Ndebele" },
	{ 0x5D, "Nepali" },
	{ 0x5C, "Oriya" },
	{ 0x5B, "Papamiento" },
	{ 0x5A, "Persian" },
	{ 0x59, "Punjabi" },
	{ 0x58, "Pushtu" },
	{ 0x57, "Quechua" },
	{ 0x56, "Russian" },
	{ 0x55, "Ruthenian" },
	{ 0x54, "Serbo-Croat" },
	{ 0x53, "Shoma" },
	{ 0x52, "Sinhalese" },
	{ 0x51, "Somali" },
	{ 0x50, "Sranan Tongo" },
	{ 0x4F, "Swahili" },
	{ 0x4E, "Tadzhik" },
	{ 0x4D, "Tamil" },
	{ 0x4C, "Tatar" },
	{ 0x4B, "Telugu" },
	{ 0x4A, "Thai" },
	{ 0x49, "Ukranian" },
	{ 0x48, "Urdu" },
	{ 0x47, "Uzbek" },
	{ 0x46, "Vietnamese" },
	{ 0x45, "Zulu" },
	{ 0x40, "Background sound / Clean feed" },
};

/****************************\
* PROGRAMME TYPE (PTY) CODES *
\****************************/

const char *pty_codes[32] ={"None", "News", "Current Afairs", "Information",
			     "Sport", "Education", "Drama", "Culture",
			     "Science", "Varied Speech", "Pop Music",
			     "Rock Music", "Easy Listening", "Light Classics",
			     "Serious Classics", "Other Music", "Weather",
			     "Finance", "Children's Progs", "Social Affairs",
			     "Religion", "Phone In", "Travel & Touring",
			     "Leisure & Hobby", "Jazz Music", "Country Music",
			     "National Music", "Oldies", "Folk Music",
			     "Documentary", "Alarm Test", "Alarm !"};

/**************\
* ENTRY POINTS *
\**************/

int
rds_codes_get_ctry_idx(const char *ctry_name)
{
	static int num_countries = sizeof(ctry_codes) / sizeof(ctry_codes[0]);
	int i = 0;

	for(i = 0; i < num_countries; i++)
		if(!strncmp(ctry_name, ctry_codes[i].name, 32))
			return i;
	return -1;
}

const char*
rds_codes_get_ctry_name(int idx)
{
	static int num_countries = sizeof(ctry_codes) / sizeof(ctry_codes[0]);

	if(idx >= num_countries)
		return "";

	return ctry_codes[idx].name;
}

int
rds_codes_get_ctry_code_by_ctry_idx(int ctry_idx)
{
	static int num_countries = sizeof(ctry_codes) / sizeof(ctry_codes[0]);
	const char *this = NULL;
	const char *next = NULL;

	if(ctry_idx >= num_countries)
		return -1;
	
	/* Check if country has multiple country codes */
	if(ctry_idx < num_countries - 2) {
		this = rds_codes_get_ctry_name(ctry_idx);
		next = rds_codes_get_ctry_name(ctry_idx + 1);
		if(!strcmp(this, next))
			return -2;
	}

	return (ctry_codes[ctry_idx].code & 0x0F00) >> 4;
}

int
rds_codes_get_ecc_by_ctry_idx(int ctry_idx)
{
	static int num_countries = sizeof(ctry_codes) / sizeof(ctry_codes[0]);
	
	if(ctry_idx >= num_countries)
		return -1;

	return (ctry_codes[ctry_idx].code & 0x00FF);
}

int
rds_codes_get_ctry_idx_from_ctry_codes(uint8_t ctry_code, uint8_t ecc)
{
	static int num_countries = sizeof(ctry_codes) / sizeof(ctry_codes[0]);
	int ctry_id = ((ctry_code & 0xF) << 8) | ecc;
	int i = 0;

	for(i = 0; i < num_countries; i++)
		if(ctry_codes[i].code == ctry_id)
			return i;

	return -1;
}

int
rds_codes_get_lang_idx(const char* lang)
{
	static int num_langs = sizeof(li_codes) / sizeof(li_codes[0]);
	int i = 0;

	for(i = 0; i < num_langs; i++)
		if(!strncmp(lang, li_codes[i].name, 32))
			return i;
	return -1;
}

const char*
rds_codes_get_lang_name(uint8_t lang_idx)
{
	static int num_langs = sizeof(li_codes) / sizeof(li_codes[0]);
	int i = 0;

	if(lang_idx >= num_langs)
		return "";

	return li_codes[lang_idx].name;
}

int
rds_codes_get_lic_by_lang_idx(uint8_t lang_idx)
{
	static int num_langs = sizeof(li_codes) / sizeof(li_codes[0]);

	if(lang_idx >= num_langs)
		return -1;

	return li_codes[lang_idx].code;
}

int
rds_codes_get_lang_idx_from_lic(uint8_t lic)
{
	static int num_langs = sizeof(li_codes) / sizeof(li_codes[0]);
	int i = 0;

	for(i = 0; i < num_langs; i++)
		if(li_codes[i].code == lic)
			return i;
	return -1;
}

const char*
rds_codes_get_pty_name(uint8_t pty)
{
	static int num_ptys = sizeof(pty_codes) / sizeof(pty_codes[0]);

	if(pty >= num_ptys)
		return "";

	return pty_codes[pty];
}
