#include <dlib/svm_threaded.h>

#include <iostream>
#include <stdio.h>
#include "cmdline.h"

using namespace std;
using namespace dlib;

typedef matrix<double, 0, 1> sample_type;  // init as 0,1 ; can be cast to arbitrary num_rows

int main(int argc, const char *argv[])
{
  /*
   * ARGUMENT PARSING
   */

  string input_file = "-";
  cmdline::parser c;

  c.add        ("help",    'h', "print this message");
  c.add<int>   ("cross-validate", 'c', "perform k-fold cross validation", false, 0);
  c.add<string>("output",  'o', "store trained classifier in file", false);
  c.add<string>("trainset",'n', "limit the training dataset to the first n samples, if n is less than or equal 1 it is interpreted the percentage of a stratified random split that is retained for training. If not a number it is interpreted as a filename containing training samples.", false, "1");
  c.footer     ("<classifier> [input-data]...");

  /* parse common arguments */
  bool parse_ok = c.parse(argc, argv, false) && !c.exist("help");

  if (!parse_ok) {
    cerr << c.usage() << endl << c.error() << endl;
    return -1;
  }

  /* check if we can open the output file */
  ofstream test(c.get<string>("output"), ios_base::out | ios_base::binary);
  ostream &output = c.exist("output") ? test : cout;

  if (c.exist("output") && !test.good()) {
    cerr << c.usage() << endl << "unable to open \"" << c.get<string>("output") << "\" as output" << endl;
    return -1;
  }

  if (c.rest().size() > 0)
    input_file = c.rest()[0];

  /* do we read from a file or stdin? */
  ifstream fin; fin.open(input_file);
  istream &in = input_file=="-" ? cin : fin;

  if (!in.good()) {
    cerr << "unable to open input file " << input_file << endl;
    return -1;
  }


  /*
   * READ TRAINING SAMPLES
   */

  std::vector<sample_type> samples;
  std::vector<string> labels;
  std::set<string> u_labels;

  string line, label;

  while (getline(in, line)) {
    stringstream ss(line);

    if (line.find_first_not_of(" \t") == string::npos) {
      if (samples.size() != 0)
        break;
      else
        continue;
    }

    if (line[0] == '#')
      continue;

    ss >> label;
    std::vector<double> sample;
    string val;
    while (ss >> val) // this also handles nan and infs correctly
      sample.push_back(strtod(val.c_str(), NULL));

    if (sample.size() == 0)
      continue;

    samples.push_back(mat(sample));
    labels.push_back(label);
    u_labels.insert(label);
  }


  //cout << "number of samples: " << samples.size() << endl;
  //cout << "number of unique labels: " << u_labels.size() << endl;


  /*
   * TRAINING
   */

  // create one vs one trainer
  one_vs_one_trainer<any_trainer<sample_type>, string> ovo_trainer;

  // create kernel ridge regression binary trainer
  krr_trainer<radial_basis_kernel<sample_type>> krr_rbf_trainer;
  krr_rbf_trainer.set_kernel(radial_basis_kernel<sample_type>(0.1));

  // set up one vs one trainer to use krr trainer
  ovo_trainer.set_trainer(krr_rbf_trainer);
  ovo_trainer.set_num_threads(8);

  // randomize and cross-validate samples
  randomize_samples(samples, labels);
  if (c.get<int>("cross-validate") > 0) {
    matrix<double> cv_result = cross_validate_multiclass_trainer(ovo_trainer, samples, labels, c.get<int>("cross-validate"));
    cout << c.get<int>("cross-validate") << "-fold cross-validation:" << endl << cv_result << endl;
  }

  // train and get the decision function
  one_vs_one_decision_function<one_vs_one_trainer<any_trainer<sample_type>, string>, decision_function<radial_basis_kernel<sample_type>>> df = ovo_trainer.train(samples, labels);

  // stream decision function to output
  serialize(df, output);

  if (!c.exist("output"))
    cout << endl; // mark the end of the classifier if piping
  else
    test.close();

}
