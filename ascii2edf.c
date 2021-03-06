/*
 ***************************************************************************
 *
 * Author: Mike Hoolehan
 *
 * Copyright (C) 2013 Mike Hoolehan
 *
 * mike@hoolehan.com
 *
 * This program converts an ascii csv file into a European Data Format file.
 * Various parameters regarding the csv file format and conversion
 * are expected to be defined by an xml file, which is a required
 * argument to this program.  This XML file is the same format supported
 * and generated by EDFbrowser. An example is
 *  <EDFbrowser_ascii2edf_template>
 *      <separator>tab</separator>
 *      <columns>2</columns>
 *      <startline>1</startline>
 *      <samplefrequency>20.0000000000</samplefrequency>
 *      <autophysicalmaximum>1</autophysicalmaximum>
 *      <edf_format>1</edf_format>
 *      <signalparams>
 *          <checked>0</checked> <!-- whether to convert or not -->
 *          <label>Heartrate</label>
 *          <physical_maximum></physical_maximum>
 *          <physical_dimension>breaths/min</physical_dimension>
 *          <multiplier>1.000000</multiplier>
 *      </signalparams>
 *      <signalparams>
 *          <checked>1</checked>
 *          <label>Flow</label>
 *          <physical_maximum></physical_maximum>
 *          <physical_dimension>l/m</physical_dimension>
 *          <multiplier>1.000000</multiplier>
 *      </signalparams>
 *  </EDFbrowser_ascii2edf_template>
 *
 *
 * This work is an adaptation of
 * EDFbrowser by Teunis van Beelen (teuniz@gmail.com)
 *
 *
 ***************************************************************************
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 ***************************************************************************
 *
 * This version of GPL is at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
 *
 ***************************************************************************
 */

#include "xml.h"
#include <stdio.h>
#include <stdlib.h>
#if !defined(__APPLE__) && !defined(__MACH__) && !defined(__APPLE_CC__)
#include <malloc.h>
#endif

#define MAX_PATH_LENGTH 1024
#define MAX_EDF_SIGNALS 128

void latin1_to_ascii(char *, int);
int loadTemplate(char * path);
void initSignalTable();

char separator; /* CSV file separator */
int columns; /* number of columns in csv */
int startline; /* which line of csv data starts */
double samplefrequency; /* frequency of csv samples */
int autoPhysicalMaximum; /* should physical maxima be autodetected */
int edf_format; /* edf/bdf format switch */
int edfsignals; /* how many signals are to be output */

/* Per signal info from template */
double physmax[MAX_EDF_SIGNALS];
double multiplier[MAX_EDF_SIGNALS];
double sensitivity[MAX_EDF_SIGNALS];
char signames[128][MAX_EDF_SIGNALS];
char sigdimensions[128][MAX_EDF_SIGNALS];
int column_enabled[MAX_EDF_SIGNALS];

int main(int argc, char *argv[]) {

	int i, j, k, p, column, column_end, headersize, temp, datarecords,
			str_start, line_nr, smpls_per_block = 0, bufsize = 0, edf_signal,
			len;

	char path[MAX_PATH_LENGTH], template_path[MAX_PATH_LENGTH],
			patient_name[128], recording[128], str[256], line[2048], *buf,
			scratchpad[128], outputfilename[MAX_PATH_LENGTH];
	double datrecduration;
	int day, month, year, hour, minute, second;

	double value[MAX_EDF_SIGNALS];
	FILE *inputfile, *outputfile;

	if (argc != 12) {
		printf( "ASCII to EDF(+) or BDF(+) converter\n"
						"Usage: ascii2edf <csv_file> <template_file> <subject_name> <recording_name> <year> <month> <day> <hour> <minute> <second> <outputfilename>\n\n");
		return (1);
	}

	strcpy(path, argv[1]);
	strcpy(template_path, argv[2]);
	strcpy(patient_name, argv[3]);
	strcpy(recording, argv[4]);
	strcpy(outputfilename, argv[11]);
	year = atoi(argv[5]);
	month = atoi(argv[6]);
	day = atoi(argv[7]);
	hour = atoi(argv[8]);
	minute = atoi(argv[9]);
	second = atoi(argv[10]);
	if (year < 0 || year > 99 || month < 1 || month > 12 || day < 1 || day > 31
			|| hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0
			|| second > 59) {
		printf("Invalid date/time specified.  All date/time fields must be 2 digits");
		return 1;
	}
	initSignalTable();

	if (!strcmp(path, "")) {
		printf("Path is null");
		return 1;
	}

	inputfile = fopen(path, "rb");
	if (inputfile == NULL ) {
		printf("Failed to open infile for reading");
		return 1;
	}

	if (!loadTemplate(template_path)) {
		return (1);
	}

	/********************** check file *************************/
	rewind(inputfile);
	temp = 0;

	for (i = 0; i < (startline - 1);) {
		temp = fgetc(inputfile);

		if (temp == EOF) {
			printf("File does not contain enough lines");
			return 1;
		}

		if (temp == '\n') {
			i++;
		}
	}
	headersize = ftell(inputfile);
	column_end = 1;
	column = 0;

	for (i = 0; i < 2046; i++) {
		temp = fgetc(inputfile);

		if (temp == EOF) {
			printf("File does not contain enough lines");
			return 1;
		}

		if (temp == '\r') {
			continue;
		}

		if (temp == separator) {
			if (!column_end) {
				column++;
				column_end = 1;
			}
		} else {
			if (temp == '\n') {
				if (!column_end) {
					column++;
				}

				if (column != columns) {
					printf("Number of columns (%d) does not match (%d)", column, columns);
					return 1;
				}

				break;
			}
			column_end = 0;
		}
	}

	if (i > 2045) {
		printf("Too many characters in a line");
		return 1;
	}

	/***************** find highest physical maximums ***********************/

	if (autoPhysicalMaximum) {
		for (i = 0; i < MAX_EDF_SIGNALS; i++) {
			physmax[i] = 0.00001;
		}

		fseek(inputfile, (long long) headersize, SEEK_SET);

		i = 0;
		column = 0;
		column_end = 1;
		str_start = 0;
		edf_signal = 0;
		line_nr = startline;

		while (1) {
			temp = fgetc(inputfile);

			if (temp == EOF) {
				break;
			}

			line[i] = temp;

			if (line[i] == '\r') {
				continue;
			}

			if (separator != ',') {
				if (line[i] == ',') {
					line[i] = '.';
				}
			}

			if (line[i] == separator) {
				if (!column_end) {
					if (column_enabled[column]) {
						value[edf_signal] = atof(line + str_start);
						edf_signal++;
					}
					column_end = 1;
					column++;
				}
			} else {
				if (line[i] != '\n') {
					if (column_end) {
						str_start = i;
						column_end = 0;
					}
				}
			}

			if (line[i] == '\n') {
				if (!column_end) {
					if (column_enabled[column]) {
						value[edf_signal] = atof(line + str_start);

						edf_signal++;
					}
					column++;
					column_end = 1;
				}

				if (column != columns) {
					for (j = 0; j < 10; j++) {
						if (fgetc(inputfile) == EOF) {
							break; /* ignore error because we reached the end of the file */
						} /* added this code because some ascii-files stop abruptly in */
					} /* the middle of a row but they do put a newline-character at the end */

					if (j < 10) {
						break;
					}

					printf("Error, number of columns in line %i is wrong.\n",
							line_nr);
					fclose(inputfile);
					return 1;
				}

				line_nr++;

				for (j = 0; j < edfsignals; j++) {
					if (value[j] < 0.0) {
						value[j] *= -1.0;
					}

					if (physmax[j] < value[j]) {
						physmax[j] = value[j];
					}
				}

				str_start = 0;
				i = 0;
				column = 0;
				column_end = 1;
				edf_signal = 0;
				continue;
			}

			i++;

			if (i > 2046) {
				printf("Error, line %i is too long.\n", line_nr);
				fclose(inputfile);
				return 1;
			}
		}
		edf_signal = 0;

		for (i = 0; i < columns; i++) {
			if (column_enabled[i]) {
				physmax[edf_signal] *= multiplier[i];

				if (physmax[edf_signal] > 9999999.0) {
					physmax[edf_signal] = 9999999.0;
				}

				if (edf_format) {
					sensitivity[edf_signal] = 32767.0 / physmax[edf_signal];
				} else {
					sensitivity[edf_signal] = 8388607.0 / physmax[edf_signal];
				}

				sensitivity[edf_signal++] *= multiplier[i];
			}
		}

	}

	/***************** write header *****************************************/

	/*outputfilename[0] = 0; */
	if (!strcmp(outputfilename, "")) {
		fclose(inputfile);
		return 1;
	}

	outputfile = fopen(outputfilename, "wb");
	if (outputfile == NULL ) {
		printf("Can not open file %s for writing.", outputfilename);
		fclose(inputfile);
		return 1;
	}

	if (edf_format) {
		fprintf(outputfile, "0       ");
	} else {
		fputc(255, outputfile);
		fprintf(outputfile, "BIOSEMI");
	}

	p = snprintf(scratchpad, 128, "%s", patient_name);
	for (; p < 80; p++) {
		scratchpad[p] = ' ';
	}
	latin1_to_ascii(scratchpad, 80);
	scratchpad[80] = 0;
	fprintf(outputfile, "%s", scratchpad);

	p = snprintf(scratchpad, 128, "%s", recording);
	for (; p < 80; p++) {
		scratchpad[p] = ' ';
	}
	latin1_to_ascii(scratchpad, 80);
	scratchpad[80] = 0;
	fprintf(outputfile, "%s", scratchpad);

	fprintf(outputfile, "%02i.%02i.%02i%02i.%02i.%02i", day, month, year, hour,
			minute, second);
	fprintf(outputfile, "%-8i", 256 * edfsignals + 256);
	fprintf(outputfile, "                                            ");
	fprintf(outputfile, "-1      ");
	if (samplefrequency < 1.0) {
		datrecduration = 1.0 / samplefrequency;
		snprintf(str, 256, "%.8f", datrecduration);
		if (fwrite(str, 8, 1, outputfile) != 1) {
			printf("Error: A write error occurred.");
			fclose(inputfile);
			fclose(outputfile);
			return 1;
		}
	} else {
		if (((int) samplefrequency) % 10) {
			datrecduration = 1.0;
			fprintf(outputfile, "1       ");
		} else {
			datrecduration = 0.1;
			fprintf(outputfile, "0.1     ");
		}
	}
	fprintf(outputfile, "%-4i", edfsignals);

	for (i = 0; i < columns; i++) {
		if (column_enabled[i]) {
			p = fprintf(outputfile, "%s", signames[i]);
			for (j = p; j < 16; j++) {
				fputc(' ', outputfile);
			}
		}
	}

	for (i = 0; i < (80 * edfsignals); i++) {
		fputc(' ', outputfile);
	}

	for (i = 0; i < columns; i++) {
		if (column_enabled[i]) {
			p = fprintf(outputfile, "%s", sigdimensions[i]);
			for (j = p; j < 8; j++) {
				fputc(' ', outputfile);
			}
		}
	}
	edf_signal = 0;

	for (i = 0; i < columns; i++) {
		if (column_enabled[i]) {
			if (autoPhysicalMaximum) {
				sprintf(str, "%.8f", physmax[edf_signal++] * -1.0);
				strcat(str, "        ");
				str[8] = 0;
				fprintf(outputfile, "%s", str);
			} else {
				fputc('-', outputfile);
				p = fprintf(outputfile, "%f", physmax[i]);
				for (j = p; j < 7; j++) {
					fputc(' ', outputfile);
				}
			}
		}
	}
	edf_signal = 0;

	for (i = 0; i < columns; i++) {
		if (column_enabled[i]) {
			if (autoPhysicalMaximum) {
				sprintf(str, "%.8f", physmax[edf_signal++]);
				strcat(str, "        ");
				str[8] = 0;
				fprintf(outputfile, "%s", str);
			} else {
				p = fprintf(outputfile, "%f", physmax[i]);
				for (j = p; j < 8; j++) {
					fputc(' ', outputfile);
				}

				if (edf_format) {
					sensitivity[edf_signal] = 32767.0 / physmax[i];
				} else {
					sensitivity[edf_signal] = 8388607.0 / physmax[i];
				}

				sensitivity[edf_signal++] *= multiplier[i];
			}
		}
	}

	for (i = 0; i < edfsignals; i++) {
		if (edf_format) {
			fprintf(outputfile, "-32768  ");
		} else {
			fprintf(outputfile, "-8388608");
		}
	}

	for (i = 0; i < edfsignals; i++) {
		if (edf_format) {
			fprintf(outputfile, "32767   ");
		} else {
			fprintf(outputfile, "8388607 ");
		}
	}

	for (i = 0; i < (80 * edfsignals); i++) {
		fputc(' ', outputfile);
	}

	if (samplefrequency < 1.0) {
		for (i = 0; i < edfsignals; i++) {
			fprintf(outputfile, "1       ");
			smpls_per_block = 1;
		}
	} else {
		if (((int) samplefrequency) % 10) {
			for (i = 0; i < edfsignals; i++) {
				fprintf(outputfile, "%-8i", (int) samplefrequency);
				smpls_per_block = (int) samplefrequency;
			}
		} else {
			for (i = 0; i < edfsignals; i++) {
				fprintf(outputfile, "%-8i", ((int) samplefrequency) / 10);
				smpls_per_block = ((int) samplefrequency) / 10;
			}
		}
	}

	for (i = 0; i < (32 * edfsignals); i++) {
		fputc(' ', outputfile);
	}

	/***************** start conversion **************************************/

	if (edf_format) {
		bufsize = smpls_per_block * 2 * edfsignals;
	} else {
		bufsize = smpls_per_block * 3 * edfsignals;
	}

	buf = (char *) calloc(1, bufsize);
	if (buf == NULL ) {
		printf("Critical error: Malloc error (buf)");
		fclose(inputfile);
		fclose(outputfile);
		return 1;
	}

	fseek(inputfile, (long long) headersize, SEEK_SET);
	i = 0;
	k = 0;
	column = 0;
	column_end = 1;
	datarecords = 0;
	str_start = 0;
	edf_signal = 0;
	line_nr = startline;

	while (1) {
		temp = fgetc(inputfile);

		if (temp == EOF) {
			break;
		}

		line[i] = temp;

		if (line[i] == '\r') {
			continue;
		}

		if (separator != ',') {
			if (line[i] == ',') {
				line[i] = '.';
			}
		}

		if (line[i] == separator) {
			if (!column_end) {
				if (column_enabled[column]) {
					value[edf_signal] = atof(line + str_start);

					edf_signal++;
				}
				column_end = 1;
				column++;
			}
		} else {
			if (line[i] != '\n') {
				if (column_end) {
					str_start = i;
					column_end = 0;
				}
			}
		}

		if (line[i] == '\n') {
			if (!column_end) {
				if (column_enabled[column]) {
					value[edf_signal] = atof(line + str_start);
					edf_signal++;
				}
				column++;
				column_end = 1;
			}

			if (column != columns) {
				for (j = 0; j < 10; j++) {
					if (fgetc(inputfile) == EOF) {
						break; /* ignore error because we reached the end of the file */
					} /* added this code because some ascii-files stop abruptly in */
				} /* the middle of a row but they do put a newline-character at the end */

				if (j < 10) {
					break;
				}

				printf("Error, number of columns in line %i is wrong.\n",
						line_nr);
				fclose(inputfile);
				fclose(outputfile);
				free(buf);
				return 1;
			}

			line_nr++;

			for (j = 0; j < edfsignals; j++) {
				temp = (int) (value[j] * sensitivity[j]);

				if (edf_format) {
					if (temp > 32767)
						temp = 32767;

					if (temp < -32768)
						temp = -32768;

					*(((short *) buf) + k + (j * smpls_per_block)) =
							(short) temp;
				} else {
					if (temp > 8388607)
						temp = 8388607;

					if (temp < -8388608)
						temp = -8388608;

					p = (k + (j * smpls_per_block)) * 3;

					buf[p++] = temp & 0xff;
					buf[p++] = (temp >> 8) & 0xff;
					buf[p] = (temp >> 16) & 0xff;
				}
			}
			k++;

			if (k >= smpls_per_block) {
				if (fwrite(buf, bufsize, 1, outputfile) != 1) {
					printf("Error: Write error during conversion.");
					fclose(inputfile);
					fclose(outputfile);
					free(buf);
					return 1;
				}
				datarecords++;
				k = 0;
			}

			str_start = 0;
			i = 0;
			column = 0;
			column_end = 1;
			edf_signal = 0;
			continue;
		}
		i++;

		if (i > 2046) {
			printf("Error, line %i is too long.\n", line_nr);
			fclose(inputfile);
			fclose(outputfile);
			free(buf);
			return 1;
		}
	}

	fseek(outputfile, 236LL, SEEK_SET);
	fprintf(outputfile, "%-8i", datarecords);
	free(buf);

	if (fclose(outputfile)) {
		printf("Error: An error occurred when closing outputfile.");
		fclose(inputfile);
		return 1;
	}

	if (fclose(inputfile)) {
		printf("Error: An error occurred when closing inputfile.");
		return 1;
	}

	printf("Done. EDF file is located at %s\n", outputfilename);

	return 0;
}

int loadTemplate(char * path) {
	int i, temp;
	/*char path[MAX_PATH_LENGTH];*/
	char *content;
	double f_temp;
	struct xml_handle *xml_hdl;

	if (!strcmp(path, "")) {
		return 1;
	}

	xml_hdl = xml_get_handle(path);
	if (xml_hdl == NULL ) {
		printf("Error Can not open template file for reading.");
		return 0;
	}

	if (strcmp(xml_hdl->elementname, "EDFbrowser_ascii2edf_template")) {
		printf("Error There seems to be an error in this template.");
		xml_close(xml_hdl);
		return 0;
	}

	if (xml_goto_nth_element_inside(xml_hdl, "separator", 0)) {
		printf("Error There seems to be an error in this template.");
		xml_close(xml_hdl);
		return 0;
	}
	content = xml_get_content_of_element(xml_hdl);
	if (!strcmp(content, "tab")) {
		separator = '\t';
		free(content);
	} else {
		if (strlen(content) != 1) {
			printf("Error There seems to be an error in this template.");
			free(content);
			xml_close(xml_hdl);
			return 0;
		} else {
			if ((content[0] < 32) || (content[0] > 126)) {
				printf("Error There seems to be an error in this template.");
				free(content);
				xml_close(xml_hdl);
				return 0;
			}
			separator = content[0];
			free(content);
		}
	}
	xml_go_up(xml_hdl);

	if (xml_goto_nth_element_inside(xml_hdl, "columns", 0)) {
		printf("Error There seems to be an error in this template.");
		xml_close(xml_hdl);
		return 0;
	}
	content = xml_get_content_of_element(xml_hdl);
	temp = atoi(content);
	free(content);
	if ((temp < 1) || (temp > 256)) {
		printf("Error There seems to be an error in this template.");
		xml_close(xml_hdl);
		return 0;
	}
	columns = temp; /*Set number of columns*/
	xml_go_up(xml_hdl);

	if (xml_goto_nth_element_inside(xml_hdl, "startline", 0)) {
		printf("Error There seems to be an error in this template.");
		xml_close(xml_hdl);
		return 0;
	}
	content = xml_get_content_of_element(xml_hdl);
	temp = atoi(content);
	free(content);
	if ((temp < 1) || (temp > 100)) {
		printf("Error There seems to be an error in this template.");
		xml_close(xml_hdl);
		return 0;
	}
	startline = temp;
	xml_go_up(xml_hdl);

	if (xml_goto_nth_element_inside(xml_hdl, "samplefrequency", 0)) {
		printf("Error There seems to be an error in this template.");
		xml_close(xml_hdl);
		return 0;
	}
	content = xml_get_content_of_element(xml_hdl);
	f_temp = atof(content);
	free(content);
	if ((f_temp < 0.0000001) || (f_temp > 1000000.0)) {
		printf("Error There seems to be an error in this template.");
		xml_close(xml_hdl);
		return 0;
	}
	samplefrequency = f_temp;
	xml_go_up(xml_hdl);

	if (!(xml_goto_nth_element_inside(xml_hdl, "autophysicalmaximum", 0))) {
		content = xml_get_content_of_element(xml_hdl);
		autoPhysicalMaximum = atoi(content);
		free(content);
		if ((autoPhysicalMaximum < 0) || (autoPhysicalMaximum > 1)) {
			autoPhysicalMaximum = 1;
		}
		xml_go_up(xml_hdl);
	}

	if (!(xml_goto_nth_element_inside(xml_hdl, "edf_format", 0))) {
		content = xml_get_content_of_element(xml_hdl);
		edf_format = atoi(content);
		free(content);
		if ((edf_format < 0) || (edf_format > 1)) {
			edf_format = 0;
		}
		xml_go_up(xml_hdl);
	}

	for (i = 0; i < columns; i++) {
		if (xml_goto_nth_element_inside(xml_hdl, "signalparams", i)) {
			printf("Error There seems to be an error in this template.");
			xml_close(xml_hdl);
			return 0;
		}

		if (xml_goto_nth_element_inside(xml_hdl, "checked", 0)) {
			printf("Error There seems to be an error in this template.");
			xml_close(xml_hdl);
			return 0;
		}
		content = xml_get_content_of_element(xml_hdl);
		if (!strcmp(content, "0")) {
			column_enabled[i] = 0;
		} else {
			column_enabled[i] = 1;
			edfsignals++;
		}
		free(content);
		xml_go_up(xml_hdl);

		if (xml_goto_nth_element_inside(xml_hdl, "label", 0)) {
			printf("Error There seems to be an error in this template.");
			xml_close(xml_hdl);
			return 0;
		}
		content = xml_get_content_of_element(xml_hdl);
		strcpy(signames[i], content);
		free(content);
		xml_go_up(xml_hdl);

		if (xml_goto_nth_element_inside(xml_hdl, "physical_maximum", 0)) {
			printf("Error There seems to be an error in this template.");
			xml_close(xml_hdl);
			return 0;
		}
		content = xml_get_content_of_element(xml_hdl);
		physmax[i] = atof(content);
		free(content);
		xml_go_up(xml_hdl);

		if (xml_goto_nth_element_inside(xml_hdl, "physical_dimension", 0)) {
			printf("Error There seems to be an error in this template.");
			xml_close(xml_hdl);
			return 0;
		}
		content = xml_get_content_of_element(xml_hdl);
		strcpy(sigdimensions[i], content);
		free(content);
		xml_go_up(xml_hdl);

		if (xml_goto_nth_element_inside(xml_hdl, "multiplier", 0)) {
			printf("Error There seems to be an error in this template.");
			xml_close(xml_hdl);
			return 0;
		}
		content = xml_get_content_of_element(xml_hdl);
		multiplier[i] = atof(content);
		free(content);
		xml_go_up(xml_hdl);
		xml_go_up(xml_hdl);
	}
	xml_close(xml_hdl);
	return 1;
}

void initSignalTable() {
	int i;
	edfsignals = 0;
	for (i = 0; i < MAX_EDF_SIGNALS; i++) {
		physmax[i] = 0;
		multiplier[i] = 1.000;
		column_enabled[i] = 0;
	}
}

void latin1_to_ascii(char *str, int len) {
  int i, value;
  for(i=0; i<len; i++) {
    value = *((unsigned char *)(str + i));
    if((value>31)&&(value<127)) {
    	continue;
    }

    switch(value) {
      case 128 : str[i] = 'E';  break;
      case 130 : str[i] = ',';  break;
      case 131 : str[i] = 'F';  break;
      case 132 : str[i] = '\"';  break;
      case 133 : str[i] = '.';  break;
      case 134 : str[i] = '+';  break;
      case 135 : str[i] = '+';  break;
      case 136 : str[i] = '^';  break;
      case 137 : str[i] = 'm';  break;
      case 138 : str[i] = 'S';  break;
      case 139 : str[i] = '<';  break;
      case 140 : str[i] = 'E';  break;
      case 142 : str[i] = 'Z';  break;
      case 145 : str[i] = '`';  break;
      case 146 : str[i] = '\'';  break;
      case 147 : str[i] = '\"';  break;
      case 148 : str[i] = '\"';  break;
      case 149 : str[i] = '.';  break;
      case 150 : str[i] = '-';  break;
      case 151 : str[i] = '-';  break;
      case 152 : str[i] = '~';  break;
      case 154 : str[i] = 's';  break;
      case 155 : str[i] = '>';  break;
      case 156 : str[i] = 'e';  break;
      case 158 : str[i] = 'z';  break;
      case 159 : str[i] = 'Y';  break;
      case 171 : str[i] = '<';  break;
      case 180 : str[i] = '\'';  break;
      case 181 : str[i] = 'u';  break;
      case 187 : str[i] = '>';  break;
      case 191 : str[i] = '\?';  break;
      case 192 : str[i] = 'A';  break;
      case 193 : str[i] = 'A';  break;
      case 194 : str[i] = 'A';  break;
      case 195 : str[i] = 'A';  break;
      case 196 : str[i] = 'A';  break;
      case 197 : str[i] = 'A';  break;
      case 198 : str[i] = 'E';  break;
      case 199 : str[i] = 'C';  break;
      case 200 : str[i] = 'E';  break;
      case 201 : str[i] = 'E';  break;
      case 202 : str[i] = 'E';  break;
      case 203 : str[i] = 'E';  break;
      case 204 : str[i] = 'I';  break;
      case 205 : str[i] = 'I';  break;
      case 206 : str[i] = 'I';  break;
      case 207 : str[i] = 'I';  break;
      case 208 : str[i] = 'D';  break;
      case 209 : str[i] = 'N';  break;
      case 210 : str[i] = 'O';  break;
      case 211 : str[i] = 'O';  break;
      case 212 : str[i] = 'O';  break;
      case 213 : str[i] = 'O';  break;
      case 214 : str[i] = 'O';  break;
      case 215 : str[i] = 'x';  break;
      case 216 : str[i] = 'O';  break;
      case 217 : str[i] = 'U';  break;
      case 218 : str[i] = 'U';  break;
      case 219 : str[i] = 'U';  break;
      case 220 : str[i] = 'U';  break;
      case 221 : str[i] = 'Y';  break;
      case 222 : str[i] = 'I';  break;
      case 223 : str[i] = 's';  break;
      case 224 : str[i] = 'a';  break;
      case 225 : str[i] = 'a';  break;
      case 226 : str[i] = 'a';  break;
      case 227 : str[i] = 'a';  break;
      case 228 : str[i] = 'a';  break;
      case 229 : str[i] = 'a';  break;
      case 230 : str[i] = 'e';  break;
      case 231 : str[i] = 'c';  break;
      case 232 : str[i] = 'e';  break;
      case 233 : str[i] = 'e';  break;
      case 234 : str[i] = 'e';  break;
      case 235 : str[i] = 'e';  break;
      case 236 : str[i] = 'i';  break;
      case 237 : str[i] = 'i';  break;
      case 238 : str[i] = 'i';  break;
      case 239 : str[i] = 'i';  break;
      case 240 : str[i] = 'd';  break;
      case 241 : str[i] = 'n';  break;
      case 242 : str[i] = 'o';  break;
      case 243 : str[i] = 'o';  break;
      case 244 : str[i] = 'o';  break;
      case 245 : str[i] = 'o';  break;
      case 246 : str[i] = 'o';  break;
      case 247 : str[i] = '-';  break;
      case 248 : str[i] = '0';  break;
      case 249 : str[i] = 'u';  break;
      case 250 : str[i] = 'u';  break;
      case 251 : str[i] = 'u';  break;
      case 252 : str[i] = 'u';  break;
      case 253 : str[i] = 'y';  break;
      case 254 : str[i] = 't';  break;
      case 255 : str[i] = 'y';  break;
      default  : str[i] = ' ';  break;
    }
  }
}
