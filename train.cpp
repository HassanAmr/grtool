#include <GRT.h>
#include <iostream>
#include "cmdline.h"
#include "libgrt_util.h"
#include "grt_crf.h"

using namespace GRT;
using namespace std;

Classifier *apply_cmdline_args(string,cmdline::parser&,int,string&);
string list_classifiers();
InfoLog info;

int main(int argc, const char *argv[])
{
  Classifier *classifier = NULL;
  string input_file = "-";
  cmdline::parser c;

  c.add<int>   ("verbose",    'v', "verbosity level: 0-4", false, 1);
  c.add        ("help",       'h', "print this message");
  c.add<string>("type",       't', "force classification, regression or timeseries input", false, "", cmdline::oneof<string>("classification", "regression", "timeseries", "unlabelled"));
  c.add<string>("output",     'o', "store trained classifier in file", true);
  c.add<float> ("num-samples",'n', "limit the training dataset to the first n samples, if n is less than or equal 1 it is interpreted the percentage of a stratified random split that is retained for training", false, 1.);
  c.footer     ("<classifier or file> [input-data]...");

  /* parse common arguments */
  bool parse_ok = c.parse(argc, argv, false)  && !c.exist("help");
  set_verbosity(c.get<int>("verbose"));

  /* got a trainable classifier? */
  string str_classifier = c.rest().size() > 0 ? c.rest()[0] : "list";
  if (str_classifier == "list") {
    cout << c.usage() << endl;
    cout << list_classifiers();
    return 0;
  }

  /* add the classifier specific arguments */
  classifier = apply_cmdline_args(str_classifier,c,1,input_file);

  if (!parse_ok) {
    cerr << c.usage() << endl << c.error() << endl;
    return -1;
  }

  if (classifier == NULL) {
    cerr << "error: unable to load/create algorithm: " << str_classifier << endl;
    return -1;
  }

  if (!c.exist("output")) {
    cerr << "please provide an output file" << endl;
    return -1;
  }

  /* check if we can open the output file */
  fstream test(c.get<string>("output"), ios_base::out);
  if (!test.good()) {
    cerr << c.usage() << endl << "unable to open \"" << c.get<string>("output") << "\" as output" << endl;
    return -1;
  }

  /* do we read from a file or stdin? */
  ifstream fin; fin.open(input_file);
  istream &in = input_file=="-" ? cin : fin;

  if (!in.good()) {
    cerr << "unable to open input file " << input_file << endl;
    return -1;
  }

  /* now start to read input samples */
  string type = c.get<string>("type");
  CsvIOSample io(type);
  CollectDataset dataset;

  /* check if the number of input is limited */
  float input_limit   = c.get<float>("num-samples");
    int input_limit_i = input_limit <= 1 ? 0 : input_limit,
        num_samples   = 0;

  while (in >> io && (input_limit_i==0 || num_samples < input_limit_i) ) {
    bool ok = false; csvio_dispatch(io, ok=dataset.add, io.labelset);
    if (!ok) {
      cerr << "error at line " << io.linenum << endl;
      exit(-1);
    }
    num_samples++;
  }

  if (num_samples==0)
    return 0;

  info << dataset.getStatsAsString() << endl;

  /* if we have percent input limit, we need to apply this now */
  TimeSeriesClassificationData t_testdata;
  ClassificationData c_testdata;

  if (input_limit < 1.) {
    switch(io.type) {
    case TIMESERIES:
      t_testdata = dataset.t_data.partition( input_limit * 100, true );
      break;
    case CLASSIFICATION:
      c_testdata = dataset.c_data.partition( input_limit * 100, true );
      break;
    default:
      cerr << "unknown data type" << endl;
      return -1;
    }
  }

  /* train and save classifier */
  bool ok; csvio_dispatch(dataset, ok=classifier->train);
  if (!ok) {
    cerr << "training failed" << endl;
    return -1;
  }

  /* propagate the classlabel names also */
  for (int i=0; i<io.labelset.size(); i++)
    classifier->setClassNameForLabel(i, io.labelset[i]);

  if (c.exist("output") && !classifier->save(c.get<string>("output"))) {
    cerr << "saving to " << c.get<string>("output") << " failed" << endl;
    return -1;
  } else if (!c.exist("output")) {
    // TODO classifier->saveModelToFile(o); to output stream so it can be picked
    // by pipe
  }

  /* if there is testdataset we need to print this now */
  if (input_limit < 1.) {
    switch(io.type) {
    case TIMESERIES:
      cout << "# timeseries";
      for (auto sample : t_testdata.getClassificationData()) {
        string label = t_testdata.getClassNameForCorrespondingClassLabel( sample.getClassLabel() );
        MatrixDouble &matrix = sample.getData();
        for (int i=0; i<matrix.getNumRows(); i++) {
          cout << endl << label;
          for (int j=0; j<matrix.getNumCols(); j++)
            cout << "\t" << matrix[i][j];
        }
        cout << endl;
      }
      break;
    case CLASSIFICATION:
      cout << "# classification" << endl;
      for (auto sample : c_testdata.getClassificationData()) {
        string label = c_testdata.getClassNameForCorrespondingClassLabel( sample.getClassLabel() );
        cout << label;
        for (auto val : sample.getSample())
          cout << "\t" << val;
        cout << endl;
      }
      break;
    default:
      cerr << "unknown data type" << endl;
      return -1;
    }
  } else if (input_limit > 1.) {
    cout << "# " << io.type << endl;
    string line;
    while (getline(in, line))
      cout << line << endl;
  } else
    return 0;
}

string list_classifiers() {
  vector<string> names = Classifier::getRegisteredClassifiers();
  stringstream ss;
  string name;

  cout << "HMM (timeseries)" << endl;
  cout << "cHMM (timeseries)" << endl;

  for (auto name : names)  {
    if ("HMM" == name) continue;
    Classifier *c = Classifier::createInstanceFromString(name);
    if (c->getTimeseriesCompatible())
      ss << name << " (timeseries" << (c->getSupportsNullRejection() ? ",null rejection)" : ")") << endl;
  }

  for (auto name : names)  {
    Classifier *c = Classifier::createInstanceFromString(name);
    if (!c->getTimeseriesCompatible())
      ss << name << (c->getSupportsNullRejection() ? "(null rejection)" : "") << endl;
  }

  return ss.str();
}

#define checkedarg(func, type, name) if(!func(p.get<type>(name))) { cerr << "invalid value for" << name << " " << p.get<type>(name) << endl; return NULL; }

Classifier *apply_cmdline_args(string name,cmdline::parser& c,int num_dimensions,string &input_file)
{
  cmdline::parser p;
  Classifier *o = NULL;

  // TODO
  // ANBC
  // AdaBoost
  // BAG
  // DecisionTree
  // GMM
  // MinDist
  // RandomForests
  // SVM
  // Softmax
  // SwipeDetector

  if ( "HMM" == name ) {
    p.add<string>("hmmtype",      'T', "either 'ergodic' or 'leftright' (default: ergodic)",  false, "leftright", cmdline::oneof<string>("leftright", "ergodic"));

    p.add<double>("delta",          0, "delta for leftright model, default: 1", false, 1);
    p.add<int>   ("num-states",   'S', "number of states", false, 10);
    p.add<int>   ("num-symbols",  'N', "number of symbols", false, 20);
    p.add<int>   ("max-epochs",     0, "maximum number of epochs during training", false, 1000);
    p.add<float> ("min-change",     0, "minimum change before abortion", false, 1.0e-5);
  } else if ( "cHMM" == name ) {
    p.add<string>("hmmtype",      'T', "either 'ergodic' or 'leftright' (default: ergodic)",  false, "ergodic", cmdline::oneof<string>("leftright", "ergodic"));

    p.add<int>   ("comitteesize",  0, "number of models used for prediction, default: 10", false, 10);
    p.add<double>("delta",         0, "delta for leftright model, default: 1", false, 1);
    p.add<int>   ("downsample",    0, "downsample factor, default: 5", false, 5);
  } else if ( "CRF" == name ) {
    p.add<string>("algorithm",   'A', "learning algorithm for training", false, "lbfgs", cmdline::oneof<string>("lbfgs", "l2sgd", "ap", "pa", "arow"));
    p.add<double>("minfreq",       0, "minimum frequency of features", false, 0.);
    p.add        ("states",      'S', "generate all possible states");
    p.add        ("transitions", 'T', "generate all possible transitions");

    // TODO
  } else if ( "KNN" == name ) {
    p.add<string>("distance",         'D', "either 'euclidean', 'cosine' or 'manhatten'", false, "euclidean", cmdline::oneof<string>("euclidean", "cosine", "manhattan"));
    p.add<double>("null-coefficient", 'N', "delta for NULL-class rejection, 0.0 means off", false, 0.0);
    p.add<int>   ("K-neighbors",      'K', "number of neighbors used in classification (if 0 search for optimum)", false, 0);
    p.add<int>   ("min-K",             0, "only used during search", false, 2);
    p.add<int>   ("max-K",             0, "only used during search", false, 20);
  } else if ( "DTW" == name ) {
    p.add<double>("null-coefficient", 'N', "delta for NULL-class rejection, 0.0 means off", false, 0.0);
    p.add<string>("rejection-mode",   'R', "NULL-class rejection mode, one of 'template', 'likelihood' or 'template_and_likelihood'", false, "template", cmdline::oneof<string>("template", "likelihood", "template_and_likelihood"));
    p.add<double>("warping-radius",   'W', "limit the warping to this radius (0 means disabled, 1 is maximum)", false, 0, cmdline::range(0.,1.));
    p.add        ("offset-by-first",  'O', "offset all samples by first sample, helps DTW when not using normalization");
    p.add<int>   ("downsample",        0, "downsample factor, default: 5", false, 5);
  } else if ( "FiniteStateMachine" == name ) {
    p.add<int>   ("num-particles",        'N', "number of particles", false, 200);
    p.add<int>   ("num-clusters",         'M', "number of clusters per state", false, 10);
    p.add<double>("transition-smoothing", 'T', "state transition smoothing", false, 0);
    p.add<double>("measurement-noise",    'S', "measurement noise", false, 10.);
  } else if ( "ParticleClassifier" == name ) {
    p.add<int>   ("num-particles",        'N', "number of particles", false, 200);
    p.add<double>("measurement-noise",    'S', "measurement noise", false, 10.);
    p.add<double>("transition-sigma",     'T', "transition sigma", false, 0.005);
    p.add<double>("phase-sigma",          'P', "phase sigma", false, 0.1);
    p.add<double>("velocity-sigma",       'V', "velocity sigma", false, 0.01);
  }

  if (!p.parse(c.rest()) || c.exist("help")) {
    cerr << c.usage() << endl << name << " options:" << endl << p.str_options() << endl << p.error() << endl;
    exit(-1);
  }

  if ( "HMM" == name ) {
    HMM *h = new HMM(
      /* hmmtype */ HMM_DISCRETE,
      /* hmmodel */ p.get<string>("hmmtype").find("ergodic")  == string::npos,
      /* delta */   p.get<double>("delta"),
      /* scaling */ false,
      /* useNull */ false);

    checkedarg(h->setNumStates, int, "num-states");
    checkedarg(h->setNumSymbols, int, "num-symbols");
    checkedarg(h->setMaxNumEpochs, int, "max-epochs");
    checkedarg(h->setMinChange, float, "min-change");

    o = h;
  } else if ( "cHMM" == name ) {
    HMM *h = new HMM(
      /* hmmtype */ HMM_CONTINUOUS,
      /* hmmodel */ p.get<string>("hmmtype").find("ergodic")  == string::npos,
      /* delta */   p.get<double>("delta"),
      /* scaling */ false,
      /* useNull */ false);

    checkedarg(h->setCommitteeSize, int, "comitteesize");
    checkedarg(h->setDownsampleFactor, int, "downsample");

    o = h;
  } else if ( "CRF" == name ) {
    CRF *c = new CRF(
        p.get<string>("algorithm"),
        p.get<double>("minfreq"),
        p.exist("states"),
        p.exist("transitions"));

    o = c;
  } else if ( "KNN" == name ) {
    KNN *k = new KNN(
      /* K */           p.get<int>("K-neighbors"),
      /* useScaling */  false,
      /* nullReject */  p.get<double>("null-coefficient") != 0,
      /* coeff */       p.get<double>("null-coefficient"),
      /* search */      p.get<int>("K-neighbors")==0,
      /* minK */        p.get<int>("min-K"),
      /* maxK */        p.get<int>("max-K"));

    string distance = p.get<string>("distance");
    if      ( "euclidean" == distance ) k->setDistanceMethod(KNN::EUCLIDEAN_DISTANCE);
    else if ( "cosine" == distance )    k->setDistanceMethod(KNN::COSINE_DISTANCE);
    else if ( "manhattan" == distance ) k->setDistanceMethod(KNN::MANHATTAN_DISTANCE);

    o = k;
  } else if ( "DTW" == name ) {
    vector<string> list = {"template","likelihoods","template_and_likelihood"};
    o = new DTW(
      /* useScaling */ false,
      /* useNullRejection */ p.get<double>("null-coefficient")!=0,
      /* nullRejectionCoeff */ p.get<double>("null-coefficient"),
      /* rejectionMode */ find(list.begin(), list.end(), p.get<string>("rejection-mode")) - list.begin(),
      /* constrainWarpingPath */ p.get<double>("warping-radius")!=0,
      /* radius */ p.get<double>("warping-radius"),
      /* offsetUsingFirstSample */ p.exist("offset-by-first"),
      /* useSmoothing */ true,
      /* smoothingFactor */ p.get<int>("downsample"));
  } else if ( "FiniteStateMachine" == name ) {
    o = new FiniteStateMachine(
      p.get<int>("num-particles"),
      p.get<int>("num-clusters"),
      p.get<double>("transition-smoothing"),
      p.get<double>("measurement-noise"));
  } else if ( "ParticleClassifier" == name ) {
    o = new ParticleClassifier(
      p.get<int>("num-particles"),
      p.get<double>("measurement-noise"),
      p.get<double>("transition-sigma"),
      p.get<double>("phase-sigma"),
      p.get<double>("velocity-sigma"));
  } else {
    o = loadClassifierFromFile(name);
  }

  if (o != NULL)
    o->setNumInputDimensions(num_dimensions);

  if (p.rest().size() > 0)
    input_file = p.rest()[0];

  return o;
}
