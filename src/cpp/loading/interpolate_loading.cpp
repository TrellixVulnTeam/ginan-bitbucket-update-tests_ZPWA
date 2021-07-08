/*!
 * @author Sébastien Allgeyer 
 * @date 9/4/21
 */

/**
 * @file make_otl_blq.cpp
 *
 * @brief Program to compute the ocean tide loading from a tide grid
 *
 * With an input data containing the list of the tide file, the Green's function and the coordinate of the site of interest,
 * this code will compute the loading (response to the solid earth to the load generated by the tide) displacement to the tide.
 * It include the vertical, N-S and E-W load in amplitude and phase at the tidal frequencies.
 *
 * @version 0.2
 *
 * @todo Clean the code
 * @todo Documentation.
 *
 * @author Sebastien Allgeyer
 * @date 26/02/2021
 */


#include <iostream>
#include <fstream>
#include <omp.h>

#include <yaml-cpp/yaml.h>
#include <boost/timer/timer.hpp>
#include <boost/program_options.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/trivial.hpp>
#include <boost/multi_array.hpp>

#include "tide.h"
#include "loading.h"
#include "utils.h"
#include "input_otl.h"
#include "load_functions.h"
#include "loadgrid.h"

using namespace std;
using namespace boost::timer;
namespace po = boost::program_options;

const int THREAD_COUNT = 8;

void program_options(int argc, char * argv[], otl_input & input)
{
	po::options_description desc{"make_otl_blq <options>"};

	// Do not set default values here, as this will overide the configuration file opitions!!!
	desc.add_options()
			("help", 			"This help message")
			("quiet", 			"Less output")
			("verbose", 		"More output")
			("config", 	 	po::value<std::string>(),	"Configuration file, This specifies the location of green function, and netcdf files for the ocean loading terms")
			("location", 	po::value<std::vector<float>>()->multitoken(), "location: lon (decimal degrees) lat (decimal degrees)")
			("code",     	po::value<std::string>(), "Station Code with or without DOMES number (ALIC 50137M0014)")
			("input",		po::value<std::string>(),	"input file containing list of stations CSV format name, lon, lat")
			("output",		po::value<std::string>(),	"Output BLQ file")

			;

	po::variables_map vm;
	// This is to be able to parse negative numbers with boost.
	po::store(po::command_line_parser(argc, argv).options(desc).style(
			po::command_line_style::unix_style ^ po::command_line_style::allow_short
	).run(), vm);
	po::notify(vm);


	if (vm.count("help")) {
		cout << "Usage: make_otl_blq [options]\n";
		cout << desc << "\n\n";
		cout << "Example: make_otl_blq --config otl.yaml --code 'ALIC 50137M0014' --location 133.8855 -23.6701 \n\n";
		cout << "Example with input file: make_otl_blq --config otl.yaml --input station.csv\n";
		cout << "                         Where station.csv contains station informations  \n";
		cout << "                           format: station name, longitude, latitude\n\n";

		exit(0);
	}

	// Parser
	std::string config_f;
	std::string code_f;
	std::vector<float> location;
	if (vm.count("config"))
	{
		config_f = vm["config"].as<std::string>();
	}
	if (vm.count("location")) {
		location = vm["location"].as<std::vector<float>>();
		input.lon.push_back(location[0]);
		input.lat.push_back(location[1]);

		if (vm.count("code")) {
			input.code.push_back(vm["code"].as<std::string>());
		} else { input.code.push_back("XXXX"); }
	}
	YAML::Node config = YAML::LoadFile(config_f );

	// input.green = config["greenfunction"].as<string>();

	YAML::Node tidefiles = config["tide"];
	for (YAML::const_iterator it = tidefiles.begin() ; it != tidefiles.end(); ++it) {
		input.tide_file.push_back(it->as<std::string>(""));
	};

	vector <string> row;
	string word;
	if (vm.count("input"))
	{
		string filename = vm["input"].as<std::string>();
		std::ifstream infile(filename);
		while(infile) {
			string line;
			if (!getline(infile, line)) break;
			if (line.size()!= 0 and line.at(0) != '#')
			{
				istringstream data(line);
				row.clear();
				while (getline(data, word, ',')) {
					row.push_back(word);
				}
				if (row.size() == 3)
				{
					input.code.push_back(row[0]);
					input.lon.push_back(stof(row[1]));
					input.lat.push_back(stof(row[2]));
				}
				row.clear();
				data.clear();
			}
		}
	}

	if (vm.count("output")) {
		input.output_blq_file = vm["output"].as<std::string>();
	} else {
		input.output_blq_file = "output.blq";
	}


};



int main(int argc, char * argv[]) {
	try {
		otl_input input;
		boost::log::core::get()->set_filter(boost::log::trivial::severity >= boost::log::trivial::info);
		boost::log::add_console_log(std::cout, boost::log::keywords::format = "%Message%");

		program_options(argc, argv, input);

		BOOST_LOG_TRIVIAL(info) << " ======== INPUT PARAMETERS ======= " << "\n";
		BOOST_LOG_TRIVIAL(info) << " - Will process the following station:  " << "\n";
		for (int i = 0; i < input.code.size(); i++)
			BOOST_LOG_TRIVIAL(info) << "   * " << input.code[i] << " -- lon, lat -- " << input.lon[i] << ", "
									<< input.lat[i] << "\n";
		BOOST_LOG_TRIVIAL(info) << " - Green's function is :  " << "\n" << "   * " << input.green << "\n";
		BOOST_LOG_TRIVIAL(info) << " - Tide files is :  " << "\n";
		for (int i = 0; i < input.tide_file.size(); i++)
			BOOST_LOG_TRIVIAL(info) << "   * " << input.tide_file[i] << "\n";
		BOOST_LOG_TRIVIAL(info) << " - Output file is:  " << "\n" << "   * " << input.output_blq_file << "\n";
		BOOST_LOG_TRIVIAL(info) << " ========       END       ======= " << "\n";


		cpu_timer timer;
		loadGrid tideinfo;

		tideinfo.set_name(input.tide_file[0]);
		tideinfo.read();


		BOOST_LOG_TRIVIAL(debug) << "there is " << tideinfo.get_nwave() << " tides\n";
		input.wave_names = tideinfo.get_wave_names();
		
//		loading load;
//		load.set_name(input.green);
//		load.read();
		BOOST_LOG_TRIVIAL(info) << "All file read \n\t" << timer.format();
		// cout << tideinfo.get_lat(0) << "  " << tideinfo.get_lon(0) << "\n\n";
		// for (int iw = 0; iw < tideinfo.get_nwave()*6; iw ++)
		// 	cout << tideinfo.interpolate(iw, input.lon[0] , input.lat[0]) << "\t";
		// cout << "\n";
		//cout << tideinfo.interpolate(1, input.lon[0], input.lat[0]) << "\n \n";

		input.out_disp.resize(boost::extents[input.code.size()][tideinfo.get_nwave()][3]) ;
	    std::fill(input.out_disp.data(), input.out_disp.data() + input.out_disp.num_elements(), std::complex<float> (0,0));

		for (int i_sta = 0 ; i_sta < input.lat.size(); i_sta++)
			for ( int i_wave = 0 ; i_wave < tideinfo.get_nwave(); i_wave ++ )
				for (int i_dir = 0 ; i_dir < 3; i_dir++ )
					input.out_disp[i_sta][i_wave][i_dir] = std::complex<float> (tideinfo.interpolate(i_wave*6 + 2*i_dir, input.lon[i_sta], input.lat[i_sta]) ,
																				tideinfo.interpolate(i_wave*6 + 2*i_dir +1, input.lon[i_sta], input.lat[i_sta])
																				);

		// for (int i_sta = 0 ; i_sta < input.lat.size(); i_sta++)
		// 	for ( int i_wave = 0 ; i_wave < tideinfo.get_nwave(); i_wave ++ )		
		// 		for (int i_dir = 0 ; i_dir < 3; i_dir++ )
		// 			cout << std::arg( input.out_disp[i_sta][i_wave][i_dir] )* 180 / M_PI  << "\t";
		// cout << "\n";
		write_BLQ(&input, 0);

	}
	catch (std::exception &e)
	{
		std::cerr << "\n\tERROR:\n\t\t " << e.what() << "\n";
		return -1;
	}
	return 0;
}
