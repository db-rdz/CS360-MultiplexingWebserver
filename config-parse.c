#include "config-parse.h"

void parseConfig(char *filename){

	char type[256],
		name[8],
		value[256];

	FILE *fp;
	int n_read;

	if ( ( fp = fopen( filename, "r" ) ) == NULL ) {
		perror("fopen");
		
	 	return;
	 }

	if(verbose_flag) printf("Parsing config file: %s\n", filename);
	int media_index = 0;
    int host_index = 0;
		
	while( !feof( fp ) && !ferror( fp ) ){

		n_read = fscanf( fp, "%s%s%s", type, name, value);
        if(strcmp(type, "host") == 0){
            strcpy(hosts[host_index].name, name);
            strcpy(hosts[host_index++].value, value);
            if(verbose_flag) printf("  Found Host-  %s : %s \n", name, value);
        }
        else if(strcmp(type, "media") == 0){
            strcpy(media[media_index].type, name);
            strcpy(media[media_index++].value, value);
            if(verbose_flag) printf("  Found Media-  %s : %s \n", name, value);
        }
	}


	number_of_conf_entries = media_index + host_index;
    number_of_host_entries = host_index;
    number_of_media_entries = media_index;

    if(verbose_flag) printf("\nfound %d number of entries in the config file \n", number_of_conf_entries);

    //TODO: only print if verbose mode is on...
	printConfigOptions();

	fclose(fp);

}

void printConfigOptions(){
    //print media entries
	for(int i = 0; i < number_of_media_entries; i++){
		if(verbose_flag) printf("  %s = %s \n", media[i].type, media[i].value);
	}

    for(int i = 0; i < number_of_host_entries; i++){
        if(verbose_flag) printf("  %s = %s \n", hosts[i].name, hosts[i].value);
    }
}

