// SPhaseUniqChannelNoiseService.cxx
// Jingbo Wang
// January 2019

#include "dunesim/DetSim/Service/SPhaseUniqChannelNoiseService.h"
#include "art/Framework/Services/Registry/ServiceDefinitionMacros.h"
#include <sstream>
#include "lardata/DetectorInfoServices/DetectorPropertiesService.h"
#include "lardata/Utilities/LArFFT.h"
#include "larcore/Geometry/Geometry.h"
#include "nurandom/RandomUtils/NuRandomService.h"
#include "art_root_io/TFileService.h"
#include "CLHEP/Random/JamesRandom.h"
#include "CLHEP/Random/RandFlat.h"
#include "CLHEP/Random/RandGauss.h"
#include "TH1F.h"
#include "TRandom3.h"
#include "TF1.h"
#include "TMath.h"

using std::cout;
using std::ostream;
using std::endl;
using std::string;
using std::ostringstream;
using rndm::NuRandomService;
using CLHEP::HepJamesRandom;

//**********************************************************************

SPhaseUniqChannelNoiseService::
SPhaseUniqChannelNoiseService(fhicl::ParameterSet const& pset)
: fRandomSeed(0), fLogLevel(1),
  fGausNoiseHistZ(nullptr), fGausNoiseHistU(nullptr), fGausNoiseHistV(nullptr),
  fGausNoiseChanHist(nullptr),
  fMicroBooNoiseHistZ(nullptr), fMicroBooNoiseHistU(nullptr), fMicroBooNoiseHistV(nullptr),
  fMicroBooNoiseChanHist(nullptr),
  fCohNoiseHist(nullptr), fCohNoiseChanHist(nullptr),
  m_pran(nullptr) {
  const string myname = "SPhaseUniqChannelNoiseService::ctor: ";
  fSpecifyNoisePoint = pset.get<bool>("SpecifyNoisePoint");
  fGenUniqNoise      = pset.get<bool>("GenUniqNoise");
  fNoiseArrayPoints  = pset.get<unsigned int>("NoiseArrayPoints");
  bool haveSeed      = pset.get_if_present<int>("RandomSeed", fRandomSeed);
  
  fEnableWhiteNoise  = pset.get<bool>("EnableWhiteNoise");
  fWhiteNoiseZ       = pset.get<double>("WhiteNoiseZ");
  fWhiteNoiseU       = pset.get<double>("WhiteNoiseU");
  fWhiteNoiseV       = pset.get<double>("WhiteNoiseV");
  
  fEnableGaussianNoise = pset.get<bool>("EnableGaussianNoise");
  fGausNormU     = pset.get<std::vector<float>>("GausNormU");
  fGausMeanU     = pset.get<std::vector<float>>("GausMeanU");
  fGausSigmaU    = pset.get<std::vector<float>>("GausSigmaU");
  fGausNormV     = pset.get<std::vector<float>>("GausNormV");
  fGausMeanV     = pset.get<std::vector<float>>("GausMeanV");
  fGausSigmaV    = pset.get<std::vector<float>>("GausSigmaV");
  fGausNormZ     = pset.get<std::vector<float>>("GausNormZ");
  fGausMeanZ     = pset.get<std::vector<float>>("GausMeanZ");
  fGausSigmaZ    = pset.get<std::vector<float>>("GausSigmaZ");
  
  fEnableMicroBooNoise = pset.get<bool>("EnableMicroBooNoise");
  fENOB              = pset.get<double>("EffectiveNBits");
  fWirelengthZ       = pset.get<double>("WireLengthZ");
  fWirelengthU       = pset.get<double>("WireLengthU");
  fWirelengthV       = pset.get<double>("WireLengthV");
  fNoiseFunctionParameters   = pset.get<std::vector<float>>("NoiseFunctionParameters");
  if ( fEnableMicroBooNoise ) {
    fUBNoisePoisFnc = 
    new TF1("_poisson", "[0]**(x) * exp(-[0]) / ROOT::Math::tgamma(x+1.)", 0, 30);
    double params[1] = {0.};
    params[0] = 3.30762;
    fUBNoisePoisFnc->SetParameters(params);

    fUBNoiseWrLDFnc = new TF1("_wld_f", "[0] + [1]*x", 0.0, 1000);
    double wldparams[2] = {0.};
    wldparams[0] = 0.395;
    wldparams[1] = 0.001304;
    fUBNoiseWrLDFnc->SetParameters(wldparams);

    //fUBNoiseGainFnc = 
    //  new TF1("_pfn_f1", "([0]*1/(x/1000*[8]/2) + ([1]*exp(-0.5*(((x/1000*[8]/2)-[2])/[3])**2)*exp(-0.5*pow(x/1000*[8]/(2*[4]),[5])))*[6]) + [7]", 0.0, 0.5*ntick*binWidth);
  } 
  fEnableCoherentNoise = pset.get<bool>("EnableCoherentNoise");
  fCohNoiseArrayPoints  = pset.get<unsigned int>("CohNoiseArrayPoints");
  fCohExpNorm       = pset.get<float>("CohExpNorm");
  fCohExpWidth      = pset.get<float>("CohExpWidth");
  fCohExpOffset    = pset.get<float>("CohExpOffset");
  fCohGausNorm     = pset.get<std::vector<float>>("CohGausNorm");
  fCohGausMean     = pset.get<std::vector<float>>("CohGausMean");
  fCohGausSigma    = pset.get<std::vector<float>>("CohGausSigma");
  fNChannelsPerCoherentGroup      = pset.get<unsigned int>("NChannelsPerCoherentGroup");
  if ( fEnableCoherentNoise ) {
    //--- get number of gaussians ---  
    int a = fCohGausNorm.size();
    int b = fCohGausMean.size();
    int c = fCohGausSigma.size();
    int NGausians = a<b?a:b;
    NGausians = NGausians<c?NGausians:c;
    //--- set function formula ---
    std::stringstream  name;
    name.str("");
    for(int i=0;i<NGausians;i++) {
  	name<<"["<<3*i<<"]*exp(-0.5*pow((x-["<<3*i+1<<"])/["<<3*i+2<<"],2))+";
    }
    name<<"["<<3*NGausians<<"]*exp(-x/["<<3*NGausians+1<<"]) + ["<<3*NGausians+2<<"]";
    fCohNoiseFnc = new TF1("funcGausCohsNoise",name.str().c_str(), 0, 1200);
    fCohNoiseFnc ->SetNpx(12000);
    for(int i=0;i<NGausians;i++) {
      fCohNoiseFnc ->SetParameter(3*i, fCohGausNorm.at(i));	
      fCohNoiseFnc ->SetParameter(3*i+1, fCohGausMean.at(i));	
      fCohNoiseFnc ->SetParameter(3*i+2, fCohGausSigma.at(i));	
    }
    fCohNoiseFnc ->SetParameter(3*NGausians, fCohExpNorm);
    fCohNoiseFnc ->SetParameter(3*NGausians+1, fCohExpWidth);
    fCohNoiseFnc ->SetParameter(3*NGausians+2, fCohExpOffset);

    // custom poisson  
    fCohPoisFnc = new TF1("_poisson", "[0]**(x) * exp(-[0]) / ROOT::Math::tgamma(x+1.)", 0, 30);
    // poisson mean
    double params[1] = {0.};
    params[0] = 4; // hard-coded for now. To be updated with data
    fCohPoisFnc->SetParameters(params);
  } 
  
  if ( fRandomSeed == 0 ) haveSeed = false;
  pset.get_if_present<int>("LogLevel", fLogLevel);
  int seed = fRandomSeed;
  art::ServiceHandle<art::TFileService> tfs;
  fMicroBooNoiseHistZ = tfs->make<TH1F>("MicroBoo znoise", ";Z Noise [ADC counts];", 1000,   -10., 10.);
  fMicroBooNoiseHistU = tfs->make<TH1F>("MicroBoo unoise", ";U Noise [ADC counts];", 1000,   -10., 10.);
  fMicroBooNoiseHistV = tfs->make<TH1F>("MicroBoo vnoise", ";V Noise [ADC counts];", 1000,   -10., 10.);
  fMicroBooNoiseChanHist = tfs->make<TH1F>("MicroBoo NoiseChan", ";MicroBoo Noise channel;", fNoiseArrayPoints, 0, fNoiseArrayPoints);
  fGausNoiseHistZ = tfs->make<TH1F>("Gaussian znoise", ";Z Noise [ADC counts];", 1000,   -10., 10.);
  fGausNoiseHistU = tfs->make<TH1F>("Gaussian unoise", ";U Noise [ADC counts];", 1000,   -10., 10.);
  fGausNoiseHistV = tfs->make<TH1F>("Gaussian vnoise", ";V Noise [ADC counts];", 1000,   -10., 10.);
  fGausNoiseChanHist = tfs->make<TH1F>("Gaussian NoiseChan", ";Gaussian Noise channel;", fNoiseArrayPoints, 0, fNoiseArrayPoints);
  fCohNoiseHist = tfs->make<TH1F>("Cohnoise", ";Coherent Noise [ADC counts];", 1000,   -10., 10.);                           
  fCohNoiseChanHist = tfs->make<TH1F>("CohNoiseChan", ";CohNoise channel;", fCohNoiseArrayPoints, 0, fCohNoiseArrayPoints);// III = for each instance of this class.
  string rname = "SPhaseUniqChannelNoiseService";
  if ( haveSeed ) {
    if ( fLogLevel > 0 ) cout << myname << "WARNING: Using hardwired seed." << endl;
    m_pran = new HepJamesRandom(seed);
  } else {
    if ( fLogLevel > 0 ) cout << myname << "Using NuRandomService." << endl;
    art::ServiceHandle<NuRandomService> seedSvc;
    m_pran = new HepJamesRandom;
    if ( fLogLevel > 0 ) cout << myname << "    Initial seed: " << m_pran->getSeed() << endl;
    seedSvc->registerEngine(NuRandomService::CLHEPengineSeeder(m_pran), rname);
  }
  if ( fLogLevel > 0 ) cout << myname << "  Registered seed: " << m_pran->getSeed() << endl;
  if ( !fGenUniqNoise && !fSpecifyNoisePoint ) {
    auto const clockData = art::ServiceHandle<detinfo::DetectorClocksService const>()->DataForJob();
    generateNoise(clockData);
  } else {
    makeCoherentGroupsByOfflineChannel(fNChannelsPerCoherentGroup);
    if ( !fSpecifyNoisePoint ) {
      art::ServiceHandle<geo::Geometry> geo;
      fNoiseArrayPoints = geo->Nchannels();
      fCohNoiseArrayPoints = fNCoherentGroups;
    }
  }
  if ( fLogLevel > 1 ) print() << endl;
}

//**********************************************************************

SPhaseUniqChannelNoiseService::
SPhaseUniqChannelNoiseService(fhicl::ParameterSet const& pset, art::ActivityRegistry&)
: SPhaseUniqChannelNoiseService(pset) { }

//**********************************************************************

SPhaseUniqChannelNoiseService::~SPhaseUniqChannelNoiseService() {
  const string myname = "SPhaseUniqChannelNoiseService::dtor: ";
  if ( fLogLevel > 0 ) {
    cout << myname << "Deleting random engine with seed " << m_pran->getSeed() << endl;
  }
  delete m_pran;
}

//**********************************************************************

int SPhaseUniqChannelNoiseService::addNoise(detinfo::DetectorClocksData const&,
                                        detinfo::DetectorPropertiesData const&,
                                        Channel chan, AdcSignalVector& sigs) const {
  CLHEP::RandFlat flat(*m_pran);
  CLHEP::RandGauss gaus(*m_pran);
  	
  unsigned int microbooNoiseChan = -999;
  if ( fEnableMicroBooNoise ) {
    if ( fSpecifyNoisePoint ) {
      microbooNoiseChan = fIdxNoiseArrayPoint;
    } else if ( fGenUniqNoise ) {
      microbooNoiseChan = chan;
    } else {
      microbooNoiseChan = flat.fire()*fNoiseArrayPoints;
      if ( microbooNoiseChan == fNoiseArrayPoints ) --microbooNoiseChan;
      fMicroBooNoiseChanHist->Fill(microbooNoiseChan);
    }
  }
  unsigned int gausNoiseChan = -999;
  if (fEnableGaussianNoise) {
    if ( fSpecifyNoisePoint ) {
      gausNoiseChan = fIdxNoiseArrayPoint;
    } else if ( fGenUniqNoise ) {
      gausNoiseChan = chan;
    } else {
      gausNoiseChan = flat.fire()*fNoiseArrayPoints;
      if ( gausNoiseChan == fNoiseArrayPoints ) --gausNoiseChan;
      fGausNoiseChanHist->Fill(gausNoiseChan);
    }
  }
  unsigned int cohNoisechan = -999;
  unsigned int groupNum = -999;
  if ( fEnableCoherentNoise ) {
    groupNum = getGroupNumberFromOfflineChannel(chan);
    if ( fSpecifyNoisePoint ) {
      cohNoisechan = fIdxCohNoiseArrayPoint;
    } else if ( fGenUniqNoise ) {
      cohNoisechan = groupNum;
    } else {
      cohNoisechan = getCohNoiseChanFromGroup(groupNum);
      if ( cohNoisechan == fCohNoiseArrayPoints ) cohNoisechan = fCohNoiseArrayPoints-1;
      fCohNoiseChanHist->Fill(cohNoisechan);
    }
  }
  
  art::ServiceHandle<geo::Geometry> geo;
  const geo::View_t view = geo->View(chan);
  for ( unsigned int itck=0; itck<sigs.size(); ++itck ) {
    double tnoise = 0;
    if ( view==geo::kU ) {
      if(fEnableWhiteNoise)    tnoise += fWhiteNoiseU*gaus.fire();
      if(fEnableMicroBooNoise) tnoise += fMicroBooNoiseU[microbooNoiseChan][itck];
      if(fEnableGaussianNoise) tnoise += fGausNoiseU[gausNoiseChan][itck];
      if(fEnableCoherentNoise) tnoise += fCohNoise[cohNoisechan][itck];
    } 
    else if ( view==geo::kV ) {
    	if(fEnableWhiteNoise)    tnoise = fWhiteNoiseV*gaus.fire();
      if(fEnableMicroBooNoise) tnoise += fMicroBooNoiseV[microbooNoiseChan][itck];
      if(fEnableGaussianNoise) tnoise += fGausNoiseV[gausNoiseChan][itck];
      if(fEnableCoherentNoise) tnoise += fCohNoise[cohNoisechan][itck];
    } 
    else {
    	if(fEnableWhiteNoise)    tnoise += fWhiteNoiseZ*gaus.fire();
    	if(fEnableMicroBooNoise) tnoise += fMicroBooNoiseZ[microbooNoiseChan][itck];
      if(fEnableGaussianNoise) tnoise += fGausNoiseZ[gausNoiseChan][itck];
      if(fEnableCoherentNoise) tnoise += fCohNoise[cohNoisechan][itck];
    }      
    sigs[itck] += tnoise;
  }
  return 0;
}

//**********************************************************************

ostream& SPhaseUniqChannelNoiseService::print(ostream& out, string prefix) const {
  out << prefix << "SPhaseUniqChannelNoiseService: " << endl;
  
  out << prefix << " SpecifyNoisePoint: " << fSpecifyNoisePoint << endl;  
  out << prefix << "      GenUniqNoise: " << fGenUniqNoise << endl;  
  out << prefix << "          LogLevel: " <<  fLogLevel << endl;
  out << prefix << "        RandomSeed: " <<  fRandomSeed << endl;
  out << prefix << "  NoiseArrayPoints: " << fNoiseArrayPoints << endl;
  
  out << prefix << "  EnableWhiteNoise: " << fEnableWhiteNoise   << endl;  
  out << prefix << "       WhiteNoiseZ: " << fWhiteNoiseZ << endl;
  out << prefix << "       WhiteNoiseU: " << fWhiteNoiseU << endl;
  out << prefix << "       WhiteNoiseV: " << fWhiteNoiseV << endl; 
  
  out << prefix << "EnableGaussianNoise: " << fEnableGaussianNoise  << endl;
  out << prefix << "     GausNormU: [ ";  
  for(int i=0; i<(int)fGausNormU.size(); i++) { out <<  fGausNormU.at(i) << " ";}
  out << " ]" << endl;
  out << prefix << "     GausMeanU: [ ";  
  for(int i=0; i<(int)fGausMeanU.size(); i++) { out <<  fGausMeanU.at(i) << " ";}
  out << " ]" << endl;
  out << prefix << "     GausSigmaU: [ ";  
  for(int i=0; i<(int)fGausSigmaU.size(); i++) { out <<  fGausSigmaU.at(i) << " ";}
  out << " ]" << endl;
  out << prefix << "     GausNormV: [ ";  
  for(int i=0; i<(int)fGausNormV.size(); i++) { out <<  fGausNormV.at(i) << " ";}
  out << " ]" << endl;
  out << prefix << "     GausMeanV: [ ";  
  for(int i=0; i<(int)fGausMeanV.size(); i++) { out <<  fGausMeanV.at(i) << " ";}
  out << " ]" << endl;
  out << prefix << "     GausSigmaV: [ ";  
  for(int i=0; i<(int)fGausSigmaV.size(); i++) { out <<  fGausSigmaV.at(i) << " ";}
  out << " ]" << endl;
  out << prefix << "     GausNormZ: [ ";  
  for(int i=0; i<(int)fGausNormZ.size(); i++) { out <<  fGausNormZ.at(i) << " ";}
  out << " ]" << endl;
  out << prefix << "     GausMeanZ: [ ";  
  for(int i=0; i<(int)fGausMeanZ.size(); i++) { out <<  fGausMeanZ.at(i) << " ";}
  out << " ]" << endl;
  out << prefix << "     GausSigmaZ: [ ";  
  for(int i=0; i<(int)fGausSigmaZ.size(); i++) { out <<  fGausSigmaZ.at(i) << " ";}
  out << " ]" << endl;
  
  out << prefix << "EnableMicroBooNoise: " << fEnableMicroBooNoise  << endl;
  out << prefix << "    EffectiveNBits: " << fENOB  << endl;
  out << prefix << "       WireLengthU: " << fWirelengthU  << endl;
  out << prefix << "       WireLengthV: " << fWirelengthV  << endl;
  out << prefix << "       WireLengthZ: " << fWirelengthZ  << endl;
  
  out << prefix << "EnableCoherentNoise: " << fEnableCoherentNoise   << endl;
  out << prefix << "ExpNoiseArrayPoints: " << fExpNoiseArrayPoints << endl;
  out << prefix << "CohNoiseArrayPoints: " << fCohNoiseArrayPoints << endl;
  out << prefix << "MicroBoo model parameters: [ ";  
  for(int i=0; i<(int)fNoiseFunctionParameters.size(); i++) { out <<  fNoiseFunctionParameters.at(i) << " ";}
  out << " ]" << endl;
  
  out << prefix << "     CohGausNorm: [ ";  
  for(int i=0; i<(int)fCohGausNorm.size(); i++) { out <<  fCohGausNorm.at(i) << " ";}
  out << " ]" << endl;
  out << prefix << "     CohGausMean: [ ";  
  for(int i=0; i<(int)fCohGausMean.size(); i++) { out <<  fCohGausMean.at(i) << " ";}
  out << " ]" << endl;
  out << prefix << "     CohGausSigma: [ ";  
  for(int i=0; i<(int)fCohGausSigma.size(); i++) { out <<  fCohGausSigma.at(i) << " ";}
  out << " ]" << endl;
  
  out << prefix << "  Actual random seed: " << m_pran->getSeed();
  return out;
}

//**********************************************************************

void SPhaseUniqChannelNoiseService::
generateMicroBooNoise(detinfo::DetectorClocksData const& clockData,
                      float wirelength, float ENOB,
              AdcSignalVector& noise, TH1* aNoiseHist) const {
  const string myname = "SPhaseUniqChannelNoiseService::generateMicroBooNoise: ";
  if ( fLogLevel > 1 ) {
    cout << myname << "Generating noise." << endl;
    if ( fLogLevel > 2 ) {
      cout << myname << "    Seed: " << m_pran->getSeed() << endl;
    }
  }
  // Fetch sampling rate.
  float sampleRate = sampling_rate(clockData);
  // Fetch FFT service and # ticks.
  art::ServiceHandle<util::LArFFT> pfft;
  unsigned int ntick = pfft->FFTSize();
  // width of frequencyBin in kHz
  double binWidth = 1.0/(ntick*sampleRate*1.0e-6);
  CLHEP::RandFlat flat(*m_pran);
  // Create noise spectrum in frequency.
  unsigned nbin = ntick/2 + 1;
  std::vector<TComplex> noiseFrequency(nbin, 0.);
  double pval = 0.;
  double phase = 0.;
  double rnd[3] = {0.};
  
  ////////////////////////////// MicroBooNE noise model/////////////////////////////////
  // vars
  double fitpar[9] = {0.};
  
  // calculate FFT magnitude of noise from ENOB
  //double baseline_noise = std::sqrt(ntick*1.0/12)*std::pow(2, 12 - fENOB);
  double wldValue = fUBNoiseWrLDFnc->Eval(wirelength);
  fitpar[0] = fNoiseFunctionParameters.at(0);
  fitpar[1] = fNoiseFunctionParameters.at(1);
  fitpar[2] = fNoiseFunctionParameters.at(2);
  fitpar[3] = fNoiseFunctionParameters.at(3);
  fitpar[4] = fNoiseFunctionParameters.at(4);
  fitpar[5] = fNoiseFunctionParameters.at(5);
  fitpar[6] = wldValue;
  fitpar[7] = fNoiseFunctionParameters.at(7);
  fitpar[8] = ntick;
  fUBNoiseGainFnc->SetParameters(fitpar);
  fUBNoiseGainFnc->SetNpx(1000);
  	
  for ( unsigned int i=0; i<ntick/2+1; ++i ) {
    //MicroBooNE noise model
    double pfnf1val = fUBNoiseGainFnc->Eval((i+0.5)*binWidth);
    // define FFT parameters
    //double randomizer = fUBNoisePoisFnc->GetRandom()/params[0];
    double randomizer = fUBNoisePoisFnc->GetRandom()/3.30762;
    pval = pfnf1val * randomizer;
    // random phase angle
    flat.fireArray(2, rnd, 0, 1);
    phase = rnd[1]*2.*TMath::Pi();
    TComplex tc(pval*cos(phase),pval*sin(phase));
    noiseFrequency[i] += tc;
  }
  // Obtain time spectrum from frequency spectrum.
  noise.clear();
  noise.resize(ntick,0.0);
  std::vector<double> tmpnoise(noise.size());
  pfft->DoInvFFT(noiseFrequency, tmpnoise);
  noiseFrequency.clear();
  for ( unsigned int itck=0; itck<noise.size(); ++itck ) {
    noise[itck] = sqrt(ntick)*tmpnoise[itck];
  }
  for ( unsigned int itck=0; itck<noise.size(); ++itck ) {
    aNoiseHist->Fill(noise[itck]);
  }
}

//**********************************************************************

void SPhaseUniqChannelNoiseService::
generateGaussianNoise(detinfo::DetectorClocksData const& clockData,
                      AdcSignalVector& noise, std::vector<float> gausNorm,
	                    std::vector<float> gausMean, std::vector<float> gausSigma,
	                    TH1* aNoiseHist) const {
  const string myname = "SPhaseUniqChannelNoiseService::generateGaussianNoise: ";
  if ( fLogLevel > 1 ) {
    cout << myname << "Generating Gaussian noise." << endl;  
  }
  //--- get number of gaussians ---  
  int a = gausNorm.size();
  int b = gausMean.size();
  int c = gausSigma.size();
  int NGausians = a<b?a:b;
  NGausians = NGausians<c?NGausians:c;
  //--- set function formula ---
  std::stringstream  name;
  name.str("");
  for(int i=0;i<NGausians;i++) {
  	name<<"["<<3*i<<"]*exp(-0.5*pow((x-["<<3*i+1<<"])/["<<3*i+2<<"],2))+";
  }
  name<<"0";
  TF1 *funcGausNoise = new TF1("funcGausInhNoise",name.str().c_str(), 0, 1200);
  funcGausNoise->SetNpx(12000);
  for(int i=0;i<NGausians;i++) {
    funcGausNoise->SetParameter(3*i, gausNorm.at(i));	
    funcGausNoise->SetParameter(3*i+1, gausMean.at(i));	
    funcGausNoise->SetParameter(3*i+2, gausSigma.at(i));	
  }
  
  // Fetch sampling rate.
  float sampleRate = sampling_rate(clockData);
  // Fetch FFT service and # ticks.
  art::ServiceHandle<util::LArFFT> pfft;
  unsigned int ntick = pfft->FFTSize();
  CLHEP::RandFlat flat(*m_pran);
  // Create noise spectrum in frequency.
  unsigned nbin = ntick/2 + 1;
  std::vector<TComplex> noiseFrequency(nbin, 0.);
  double pval = 0.;
  double phase = 0.;
  double rnd[2] = {0.};
  // width of frequencyBin in kHz
  double binWidth = 1.0/(ntick*sampleRate*1.0e-6);
  for ( unsigned int i=0; i<ntick/2+1; ++i ) {
    // coherent noise spectrum 
    pval = funcGausNoise->Eval((double)i*binWidth);
    // randomize amplitude within 10%
    flat.fireArray(2, rnd, 0, 1);
    pval *= 0.9 + 0.2*rnd[0];
    // randomize phase angle
    phase = rnd[1]*2.*TMath::Pi();
    TComplex tc(pval*cos(phase),pval*sin(phase));
    noiseFrequency[i] += tc;
  }
  // Obtain time spectrum from frequency spectrum.
  noise.clear();
  noise.resize(ntick,0.0);
  std::vector<double> tmpnoise(noise.size());
  pfft->DoInvFFT(noiseFrequency, tmpnoise);
  noiseFrequency.clear();
  
  for ( unsigned int itck=0; itck<noise.size(); ++itck ) {
    noise[itck] = sqrt(ntick)*tmpnoise[itck];
  }
  for ( unsigned int itck=0; itck<noise.size(); ++itck ) {
    aNoiseHist->Fill(noise[itck]);
  }
  
  //free memory
  delete funcGausNoise; funcGausNoise = 0;
}
////**********************************************************************

void SPhaseUniqChannelNoiseService::
generateCoherentNoise(detinfo::DetectorClocksData const& clockData,
                      AdcSignalVector& noise, std::vector<float> gausNorm,
	                    std::vector<float> gausMean, std::vector<float> gausSigma,
	                    float cohExpNorm, float cohExpWidth, float cohExpOffset, 
	                    TH1* aNoiseHist) const {
  const string myname = "SPhaseUniqChannelNoiseService::generateCoherentGaussianNoise: ";
  if ( fLogLevel > 1 ) {
    cout << myname << "Generating Coherent Gaussian noise." << endl;  
  }
  
  // Fetch sampling rate.
  float sampleRate = sampling_rate(clockData);
  // Fetch FFT service and # ticks.
  art::ServiceHandle<util::LArFFT> pfft;
  unsigned int ntick = pfft->FFTSize();
  CLHEP::RandFlat flat(*m_pran);
  // Create noise spectrum in frequency.
  unsigned nbin = ntick/2 + 1;
  std::vector<TComplex> noiseFrequency(nbin, 0.);
  double pval = 0.;
  double phase = 0.;
  double rnd[2] = {0.};
  // width of frequencyBin in kHz
  double binWidth = 1.0/(ntick*sampleRate*1.0e-6);
  for ( unsigned int i=0; i<ntick/2+1; ++i ) {
    // coherent noise spectrum 
    pval = fCohNoiseFnc->Eval((double)i*binWidth);
    // randomize amplitude within 10%
    flat.fireArray(2, rnd, 0, 1);
    pval *= 0.9 + 0.2*rnd[0];
    // randomize amplitude with Poisson randomizers
    // double randomizer = _poisson->GetRandom()/params[0];
    // pval *= randomizer;
    // phase information is not used in this model, but will be added soon.  
    // randomize phase angle assuming phases of different frequencies are uncorrelated
    phase = rnd[1]*2.*TMath::Pi(); 
    TComplex tc(pval*cos(phase),pval*sin(phase));
    noiseFrequency[i] += tc;
  }
  // Obtain time spectrum from frequency spectrum.
  noise.clear();
  noise.resize(ntick,0.0);
  std::vector<double> tmpnoise(noise.size());
  pfft->DoInvFFT(noiseFrequency, tmpnoise);
  noiseFrequency.clear();
  
  // Note: Assume that the frequency function is obtained from a fit 
  // of the foward FFT spectrum. In LArSoft, the forward
  // FFT (doFFT) does not scale the frequency spectrum, but the backward FFT (DoInvFFT) 
  // does scale the waveform with 1/Nticks. If the frequency function is a fit to the 
  // LArSoft FFT spectrum, no scaling factor is needed after a backward FFT. 
  // However, the noise model described here is a fit to the scaled FFT spectrum 
  // (scaled with 1./sqrt(Nticks)).
  // Therefore, after InvFFT, the waveform must be nomalized with sqrt(Nticks).
  
  for ( unsigned int itck=0; itck<noise.size(); ++itck ) {
    noise[itck] = sqrt(ntick)*tmpnoise[itck];
  }
  for ( unsigned int itck=0; itck<noise.size(); ++itck ) {
    aNoiseHist->Fill(noise[itck]);
  }
  
  //free memory
  //delete fCohNoiseFnc; funcCohNoise = 0;
}

//**********************************************************************

void SPhaseUniqChannelNoiseService::makeCoherentGroupsByOfflineChannel(unsigned int nchpergroup) {
	CLHEP::RandFlat flat(*m_pran);
  CLHEP::RandGauss gaus(*m_pran);
	art::ServiceHandle<geo::Geometry> geo;
	const unsigned int nchan = geo->Nchannels();
	fChannelGroupMap.resize(nchan);
	fNCoherentGroups = 0;
	if(nchan%nchpergroup == 0) fNCoherentGroups = nchan/nchpergroup;
	else fNCoherentGroups = nchan/nchpergroup +1;
	unsigned int cohGroupNo = 0;
	for(unsigned int chan=0; chan<nchan; chan++) {
	  cohGroupNo = chan/nchpergroup; //group number
	  fChannelGroupMap[chan] = cohGroupNo; 
	}
	fGroupCoherentNoiseMap.resize(fNCoherentGroups);
	for(unsigned int ng=0; ng<fNCoherentGroups; ng++) {
	  unsigned int cohNoiseChan = flat.fire()*fCohNoiseArrayPoints;
	  fGroupCoherentNoiseMap[ng] = cohNoiseChan;
	}
}

//**********************************************************************
void SPhaseUniqChannelNoiseService::setNumNoiseArrayPoints(unsigned int n_np, unsigned int n_cnp) {
  fNoiseArrayPoints = n_np;
  fCohNoiseArrayPoints = n_cnp;
}

//**********************************************************************
void SPhaseUniqChannelNoiseService::setIdxNoiseArrayPoints(unsigned int i_n, unsigned int i_cn) {
  fIdxNoiseArrayPoint = i_n;
  fIdxCohNoiseArrayPoint = i_cn;
}

//**********************************************************************
unsigned int SPhaseUniqChannelNoiseService::getGroupNumberFromOfflineChannel(unsigned int offlinechan) const {
  return fChannelGroupMap[offlinechan];
}

//**********************************************************************
unsigned int SPhaseUniqChannelNoiseService::getCohNoiseChanFromGroup(unsigned int cohgroup) const {
  return fGroupCoherentNoiseMap[cohgroup];
}

//**********************************************************************

void SPhaseUniqChannelNoiseService::generateNoise(detinfo::DetectorClocksData const& clockData) {
  
  if(fEnableMicroBooNoise) {
    fMicroBooNoiseZ.clear();
    fMicroBooNoiseU.clear();
    fMicroBooNoiseV.clear();
    fMicroBooNoiseZ.resize(fNoiseArrayPoints);
    fMicroBooNoiseU.resize(fNoiseArrayPoints);
    fMicroBooNoiseV.resize(fNoiseArrayPoints);
    //for ( unsigned int i=0; i<fNoiseArrayPoints; ++i ) {
    //  if(fMicroBooNoiseZ.size())std::vector<AdcSignal>().swap(fMicroBooNoiseZ[i]);
    //  if(fMicroBooNoiseU.size())std::vector<AdcSignal>().swap(fMicroBooNoiseU[i]);
    //  if(fMicroBooNoiseV.size())std::vector<AdcSignal>().swap(fMicroBooNoiseV[i]);
    //}
    for ( unsigned int i=0; i<fNoiseArrayPoints; ++i ) {
      generateMicroBooNoise(clockData, fWirelengthZ, fENOB, fMicroBooNoiseZ[i], fMicroBooNoiseHistZ);
      generateMicroBooNoise(clockData, fWirelengthU, fENOB, fMicroBooNoiseU[i], fMicroBooNoiseHistU);
      generateMicroBooNoise(clockData, fWirelengthV, fENOB, fMicroBooNoiseV[i], fMicroBooNoiseHistV);
    } 
  }
  
  if(fEnableGaussianNoise) {
    fGausNoiseU.clear();
    fGausNoiseV.clear();
    fGausNoiseZ.clear();
    fGausNoiseU.resize(fNoiseArrayPoints);
    fGausNoiseV.resize(fNoiseArrayPoints);
    fGausNoiseZ.resize(fNoiseArrayPoints);
    for ( unsigned int i=0; i<fNoiseArrayPoints; ++i ) {
      generateGaussianNoise(clockData, fGausNoiseU[i], fGausNormU, fGausMeanU, fGausSigmaU, fGausNoiseHistU);
      generateGaussianNoise(clockData, fGausNoiseV[i], fGausNormV, fGausMeanV, fGausSigmaV, fGausNoiseHistV);
      generateGaussianNoise(clockData, fGausNoiseZ[i], fGausNormZ, fGausMeanZ, fGausSigmaZ, fGausNoiseHistZ);
    }
  }
  
  if(fEnableCoherentNoise) {
    if(!fGenUniqNoise && !fSpecifyNoisePoint) {
      makeCoherentGroupsByOfflineChannel(fNChannelsPerCoherentGroup);
    }
    fCohNoise.clear();
    fCohNoise.resize(fCohNoiseArrayPoints);
    for ( unsigned int i=0; i<fCohNoiseArrayPoints; ++i ) {
      generateCoherentNoise(clockData,
                            fCohNoise[i], fCohGausNorm, fCohGausMean, fCohGausSigma,
                            fCohExpNorm, fCohExpWidth, fCohExpOffset, 
                            fCohNoiseHist);
    }
  }
}

//**********************************************************************

void SPhaseUniqChannelNoiseService::newEvent() {
  if ( fGenUniqNoise || fSpecifyNoisePoint ) {
    std::cout << " !!!! SPhaseUniqChannelNoiseService: Initializing unique noise arrays for the new event ...." << endl;
    auto const clockData = art::ServiceHandle<detinfo::DetectorClocksService const>()->DataForJob();

    float sampleRate = sampling_rate(clockData);
    // width of frequencyBin in kHz
    double binWidthXntick = 1.0/(sampleRate*1.0e-6);
    fUBNoiseGainFnc = 
      new TF1("_pfn_f1", "([0]*1/(x/1000*[8]/2) + ([1]*exp(-0.5*(((x/1000*[8]/2)-[2])/[3])**2)*exp(-0.5*pow(x/1000*[8]/(2*[4]),[5])))*[6]) + [7]", 0.0, 0.5*binWidthXntick);

    generateNoise(clockData);
  }
}

//**********************************************************************

DEFINE_ART_SERVICE_INTERFACE_IMPL(SPhaseUniqChannelNoiseService, UniqChannelNoiseService)

//**********************************************************************
