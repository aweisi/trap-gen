#include <iostream>

#include <boost/program_options.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/path.hpp>

#include "utils.hpp"

#include "analyzer.hpp"

int main(int argc, char *argv[]){
    boost::program_options::options_description desc("Memory Analyzer");
    desc.add_options()
    ("help,h", "produces the help message")
    ("operation,o", boost::program_options::value<int>(), "specifies the operation which we want to execute [1: create memory image - 2: get all modifications to a specified address - 3: gets the first modification to an address after a given simulation time 4: - gets the last modification to an address]")
    ("dump,d", boost::program_options::value<std::string>(), "the name of the dump file")
    ("outFile,f", boost::program_options::value<std::string>(), "the name of the output file for the operations which need it")
    ("address,a", boost::program_options::value<unsigned int>(), "the address of which we want to get the modifications")
    ("startTime,s", boost::program_options::value<double>(), "the time at which we want to analyze the modifications (start time if needed by the chosen option)")
    ("endTime,e", boost::program_options::value<double>(), "the end time until which we want to get the modification")
    ("memSize,m", boost::program_options::value<double>(), "the maximum memory size")
    ;

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    // Checking that the parameters are correctly specified
    if(vm.count("help") != 0){
        std::cout << desc << std::endl;
        return 0;
    }
    if(vm.count("operation") == 0){
        std::cerr << "Error, it is necessary to specify the operation which has to be executed" << std::endl;
        std::cerr << desc << std::endl;
        return -1;
    }
    if(vm.count("dump") == 0){
        std::cerr << "Error, it is necessary to specify the name of the dump file" << std::endl;
        std::cerr << desc << std::endl;
        return -1;
    }
    if(vm.count("memSize") == 0){
        std::cerr << "Error, it is necessary to specify the maximum memory size" << std::endl;
        std::cerr << desc << std::endl;
        return -1;
    }
    MemAnalyzer analyzer(vm["dump"].as<std::string>(), vm["memSize"].as<unsigned int>());
    switch(vm["operation"].as<int>()){
        case 1:{
            if(vm.count("outFile") == 0){
                std::cerr << "Error, it is necessary to specify the output file with the (1) operation" << std::endl;
                std::cerr << desc << std::endl;
                return -1;
            }
            boost::filesystem::path outFilePath = boost::filesystem::system_complete(boost::filesystem::path(vm["outFile"].as<std::string>(), boost::filesystem::native));
            if(vm.count("startTime") == 0){
                analyzer.createMemImage(outFilePath);
            }
            else{
                analyzer.createMemImage(outFilePath, vm["startTime"].as<double>());
            }
        break;}
        case 2:{
        break;}
        case 3:{
        break;}
        case 4:{
        break;}
        default:
            THROW_EXCEPTION("Error, unrecognized option " << vm["operation"].as<int>());
        break;
    }

    return 0;
}
