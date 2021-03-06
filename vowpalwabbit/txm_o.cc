/*\t

Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved. Released under a BSD (revised)
license as described in the file LICENSE.node
*/
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <sstream>

#include "reductions.h"
#include "simple_label.h"
#include "multiclass.h"

using namespace std;
using namespace LEARNER;

namespace TXM_O
{
  uint32_t ceil_log2(uint32_t k)
  {
    uint32_t i = 0;
    
    while (k > (uint32_t)(1 << i))
      i++;
    
    return i;
  }
  
  class txm_o_node_pred_type	
  {
  public:
    	
    double Ehk;	
    float norm_Ehk;
    uint32_t nk;
    uint32_t label;	
    uint32_t label_cnt2;
 
    bool operator==(txm_o_node_pred_type v){
      return (label == v.label);
    }
    
    bool operator>(txm_o_node_pred_type v){
      if(label > v.label) return true;	
      return false;
    }
    
    bool operator<(txm_o_node_pred_type v){
      if(label < v.label) return true;	
      return false;
    }
    
    txm_o_node_pred_type(uint32_t l)
    {
      label = l;
      Ehk = 0.f;
      norm_Ehk = 0;
      nk = 0;
      label_cnt2 = 0;
    }
  };
  
  typedef struct
  {
    size_t id_left;
    size_t id_right;
    //size_t level;
    size_t max_cnt2;
    size_t max_cnt2_label;

    //uint32_t ec_count;
    uint32_t min_ec_count;

    uint32_t L;
    uint32_t R;
    
    uint32_t myL;
    uint32_t myR;
    bool leaf;
    v_array<txm_o_node_pred_type> node_pred;
    
    double Eh;	
    float norm_Eh;
    uint32_t n;	
  } txm_o_node_type;
  
  struct txm_o	
  {
    uint32_t k;	
    vw* all;	
    
    v_array<txm_o_node_type> nodes;	
    	
    size_t max_nodes;
    v_array<size_t> ec_path;
    v_array<size_t> min_ec_path;
    bool ec_cnt_update;

    uint32_t nbofswaps;

    size_t ex_num;
    FILE *ex_fp;
  };	
  
  //txm_o_node_type init_node(size_t level)
  txm_o_node_type init_node()	
  {
    txm_o_node_type node; 
    
    node.id_left = 0;
    node.id_right = 0;
    node.Eh = 0;
    node.norm_Eh = 0;
    node.n = 0;
    //node.level = level;
    node.leaf = true;
    node.max_cnt2 = 0;
    node.max_cnt2_label = 0;
    //node.ec_count = 0;
    node.min_ec_count = 0;
    node.L = 0;
    node.R = 0;
    node.myL = 0;
    node.myR = 0;
    return node;
  }
  
  void init_tree(txm_o& d)
  {
    //d.nodes.push_back(init_node(0));
    d.nodes.push_back(init_node());
    d.ex_num = 0;
    d.nbofswaps = 0;
    //d.ex_fp = fopen("ex_nums.txt", "wt");
  }

  bool find_switch_nodes(txm_o& b, size_t& c, size_t& p, size_t& pp)	
  {
    c = 0;
    p = 0;
    pp = 0;
    bool lr = true;

    while(1)
      {
	b.min_ec_path.push_back(p);
	
	if(b.nodes[c].leaf)
	  {
	    return lr;
	  }
	
	pp = p;
	p = c;
	
	if(b.nodes[b.nodes[c].id_left].min_ec_count < b.nodes[b.nodes[c].id_right].min_ec_count)
	  {
	    c = b.nodes[c].id_left; 
	    lr = false;
	  }
	else
	  {
	    c = b.nodes[c].id_right;
	    lr = true;
	  }     
      }    
  }
  
  void update_min_ec_count(txm_o& b, v_array<size_t>* arr)	
  {        
    while(arr->size() > 0)
      {
	size_t p = arr->pop();
	size_t l = b.nodes[p].id_left;
	size_t r = b.nodes[p].id_right;	
	
	if(b.nodes[l].leaf)
	  b.nodes[l].min_ec_count = b.nodes[l].L + b.nodes[l].R; //b.nodes[l].ec_count;

	if(b.nodes[r].leaf)
	  b.nodes[r].min_ec_count = b.nodes[r].L + b.nodes[r].R; //b.nodes[r].ec_count;

	if(b.nodes[l].min_ec_count < b.nodes[r].min_ec_count)
	  b.nodes[p].min_ec_count = b.nodes[l].min_ec_count;
	else
	  b.nodes[p].min_ec_count = b.nodes[r].min_ec_count;

	//if(b.nodes[p].min_ec_count == 0)
	//b.nodes[p].min_ec_count = 1;
      }
  }
          
  void train_node(txm_o& b, learner& base, example& ec, size_t& cn, size_t& index)
  {
    label_data* simple_temp = (label_data*)ec.ld;    
    float left_or_right = b.nodes[cn].node_pred[index].norm_Ehk - b.nodes[cn].norm_Eh;
    size_t id_left = b.nodes[cn].id_left;
    size_t id_right = b.nodes[cn].id_right;
    
    size_t id_left_right;
    size_t id_left_right_other;	
    if(left_or_right < 0)
      {
	simple_temp->label = -1.f;
	id_left_right = id_left; 
	b.nodes[cn].myL++;
      }
    else
      {
	simple_temp->label = 1.f;
	id_left_right = id_right;
	b.nodes[cn].myR++;
      }
    
    if((b.nodes[cn].myR > 0) && (b.nodes[cn].myL > 0))
      {
	if(id_left_right == 0)
	  {	    
	    if(b.nodes.size() + 2 <= b.max_nodes)
	      {
		id_left_right = b.nodes.size();	
		//b.nodes.push_back(init_node(b.nodes[cn].level + 1));	
		//b.nodes.push_back(init_node(b.nodes[cn].level + 1));
                b.nodes.push_back(init_node());
		b.nodes.push_back(init_node());
		b.nodes[cn].id_left = id_left_right;
		id_left_right_other = id_left_right + 1;
		b.nodes[cn].id_right = id_left_right_other;

                //b.nodes[b.nodes[cn].id_left].ec_count = b.nodes[cn].L;
                //b.nodes[b.nodes[cn].id_right].ec_count = b.nodes[cn].R;
                b.nodes[id_left_right].L = b.nodes[cn].L/2;		
                b.nodes[id_left_right].R = b.nodes[cn].L - b.nodes[cn].L/2;
                b.nodes[id_left_right_other].L = b.nodes[cn].R/2;		
                b.nodes[id_left_right_other].R = b.nodes[cn].R - b.nodes[cn].R/2;

		b.nodes[id_left_right].max_cnt2_label = b.nodes[cn].max_cnt2_label;
		b.nodes[id_left_right_other].max_cnt2_label = b.nodes[cn].max_cnt2_label;

		/*if(b.nodes[cn].level + 1 > b.max_depth)
		  b.max_depth = b.nodes[cn].level + 1;*/	
	      }
	      else
	      {		
		size_t min_myR_myL;
		
		if(b.nodes[cn].R > b.nodes[cn].L)
		  min_myR_myL = b.nodes[cn].L;
		else
		  min_myR_myL = b.nodes[cn].R;

		//cout << min_myR_myL << "\t" << b.nodes[0].min_ec_count << endl;
		if(min_myR_myL > 2*b.nodes[0].min_ec_count + 1)
		{
		  size_t nc, np, npp;
	          b.min_ec_path.erase();	  
		  bool lr = find_switch_nodes(b, nc, np, npp);
		  //size_t nc_l = b.nodes[nc].level;

		  //cout << "\nSWAP!!" << endl;
		  //cout << cn << "\t" << b.nodes[cn].L + b.nodes[cn].R << "\t" << nc << "\t" << b.nodes[nc].L + b.nodes[nc].R << "\t" << np  << "\t" << b.nodes[np].L + b.nodes[np].R << "\t" << npp  << "\t" << b.nodes[npp].L + b.nodes[npp].R << endl;
		  
		  //if(cn != nc)
		  //{
		      //display_tree2(b);
	
		      //cin.ignore();

		      b.nbofswaps++;

		      if(b.nodes[npp].id_left == np)
			{
			  if(lr == false)
			    b.nodes[npp].id_left = b.nodes[np].id_right;
			  else
			    b.nodes[npp].id_left = b.nodes[np].id_left;
		  
			  //update_levels(b, b.nodes[npp].id_left, b.nodes[npp].level + 1);
			}
		      else
			{
			  if(lr == false)
			    b.nodes[npp].id_right = b.nodes[np].id_right;
			  else
			    b.nodes[npp].id_right = b.nodes[np].id_left;
			  
			  //update_levels(b, b.nodes[npp].id_right, b.nodes[npp].level + 1);
			}

		      b.nodes[cn].id_left = np;
		      b.nodes[cn].id_right = nc;
		      b.nodes[np].leaf = true;
		      b.nodes[np].id_left = 0;
		      b.nodes[np].id_right = 0;
		      b.nodes[nc].leaf = true;
		      b.nodes[nc].id_left = 0;
		      b.nodes[nc].id_right = 0;
		      //b.nodes[np].level = b.nodes[cn].level + 1;
		      //b.nodes[nc].level = b.nodes[cn].level + 1;

		      b.nodes[np].R = b.nodes[cn].R/2;
		      b.nodes[np].L = b.nodes[cn].R - b.nodes[cn].R/2;
		      b.nodes[nc].R = b.nodes[cn].L/2;
		      b.nodes[nc].L = b.nodes[cn].L - b.nodes[cn].L/2;

		      //if(b.nodes[cn].level + 1 > b.max_depth)
		      //b.max_depth = b.nodes[cn].level + 1;	    
		      
		      b.min_ec_path.pop();
		      b.min_ec_path.pop();

		      //cout << endl << b.nodes[0].min_ec_count << endl;

		      update_min_ec_count(b, &b.min_ec_path);
		      //}		    	
		  }
	      }
	  }	
      }
    base.learn(ec, cn);	

    simple_temp->label = FLT_MAX;
    base.predict(ec, cn);

    b.nodes[cn].Eh += (double)ec.partial_prediction;
    b.nodes[cn].node_pred[index].Ehk += (double)ec.partial_prediction;
    b.nodes[cn].n++;
    b.nodes[cn].node_pred[index].nk++;	
  
    b.nodes[cn].norm_Eh = b.nodes[cn].Eh / b.nodes[cn].n;          
    b.nodes[cn].node_pred[index].norm_Ehk = b.nodes[cn].node_pred[index].Ehk / b.nodes[cn].node_pred[index].nk;
    
    if(b.nodes[cn].id_left == 0)	
      b.nodes[cn].leaf = true;
    else	
      b.nodes[cn].leaf = false;	
  }
  
  void predict(txm_o& b, learner& base, example& ec)	
  {
    MULTICLASS::mc_label *mc = (MULTICLASS::mc_label*)ec.ld;
    
    label_data simple_temp;
    simple_temp.initial = 0.0;
    simple_temp.weight = mc->weight;	
    ec.ld = &simple_temp;
    size_t cn = 0;
    while(1)
      {
	if(b.nodes[cn].leaf)	
	  {	
	    ec.final_prediction = b.nodes[cn].max_cnt2_label;
	    ec.ld = mc;
	    break;	
	  }
	simple_temp.label = FLT_MAX;
	base.predict(ec, cn);
  
	if(ec.final_prediction < 0)//b.nodes[cn].Eh/b.nodes[cn].n)	
	  cn = b.nodes[cn].id_left;
	else
	  cn = b.nodes[cn].id_right;	
      }	
  }
  /*
  void save_node_stats(txm_o& d)
  {
    FILE *fp;
    uint32_t i, j;
    size_t total;
    txm_o* b = &d;
    
    fp = fopen("atxm_debug.csv", "wt");
    
    for(i = 0; i < b->nodes.size(); i++)
      {
	fprintf(fp, "Node: %4d, Level: %2d, Leaf: %1d, Eh: %7.4f, n: %6d, \n", (int) i, (int) b->nodes[i].level, (int) b->nodes[i].leaf, b->nodes[i].Eh / b->nodes[i].n, b->nodes[i].n);
	
	fprintf(fp, "Label:, ");
	for(j = 0; j < b->nodes[i].node_pred.size(); j++)
	  {
	    fprintf(fp, "%6d,", (int) b->nodes[i].node_pred[j].label);
	  }	
	fprintf(fp, "\n");
	
	fprintf(fp, "Ehk:, ");
	for(j = 0; j < b->nodes[i].node_pred.size(); j++)
	  {
	    fprintf(fp, "%7.4f,", b->nodes[i].node_pred[j].Ehk / b->nodes[i].node_pred[j].nk);
	  }	
	fprintf(fp, "\n");
	
	total = 0;
	
	fprintf(fp, "nk:, ");
	for(j = 0; j < b->nodes[i].node_pred.size(); j++)
	  {
	    fprintf(fp, "%6d,", (int) b->nodes[i].node_pred[j].nk);
	    total += b->nodes[i].node_pred[j].nk;	
	  }	
	fprintf(fp, "\n");
	
	fprintf(fp, "max(lab:cnt:tot):, %3d,%6d,%7d,\n", (int) b->nodes[i].max_cnt2_label, (int) b->nodes[i].max_cnt2, (int) total);
	fprintf(fp, "left: %4d, right: %4d", (int) b->nodes[i].id_left, (int) b->nodes[i].id_right);
	fprintf(fp, "\n\n");
      }
    
    fclose(fp);
    }*/	
  
  void learn(txm_o& b, learner& base, example& ec)
  {
    static size_t  ec_cnt = 0;
    //size_t ec_err = 3657;
  
      predict(b,base,ec);
    
    MULTICLASS::mc_label *mc = (MULTICLASS::mc_label*)ec.ld;
    b.ec_cnt_update = true;
    
    v_array<size_t> track;

    if(b.all->training && (mc->label != (uint32_t)-1) && !ec.test_only)	//if training the tree
      {
	ec_cnt++;
	if(ec_cnt % 100000 == 0)
		cout << ec_cnt << endl;	
	
	size_t index = 0;
		
	label_data simple_temp;	
	simple_temp.initial = 0.0;
	simple_temp.weight = mc->weight;
	ec.ld = &simple_temp;	
	uint32_t oryginal_label = mc->label;	
		
	size_t tmp_final_prediction = ec.final_prediction;
	size_t cn = 0;
	size_t tmp;

	while(1)
	  {
	    //if(ec_cnt ==  ec_err) cout << "\t" << cn << endl;
	    //cout << cn << "\t";
	    
	    //if(!track.contain_sorted(cn, tmp))
	    //	track.unique_add_sorted(cn);
	    //else
	    //{	
	    //	cout << "loop detected!!";
	    //	cin.ignore();
	    //}
	
	    //b.nodes[cn].ec_count++;

	    b.ec_path.push_back(cn);

	    index = b.nodes[cn].node_pred.unique_add_sorted(txm_o_node_pred_type(oryginal_label));
	    
	    b.nodes[cn].node_pred[index].label_cnt2++;
	    
	    if(b.nodes[cn].node_pred[index].label_cnt2 > b.nodes[cn].max_cnt2)
	      {
		b.nodes[cn].max_cnt2 = b.nodes[cn].node_pred[index].label_cnt2;
		b.nodes[cn].max_cnt2_label = b.nodes[cn].node_pred[index].label;
	      }
	    
	    train_node(b, base, ec, cn, index);

	    if(b.nodes[cn].leaf)
	      {	
		if(ec.final_prediction < 0)//b.nodes[cn].Eh/b.nodes[cn].n)
		  b.nodes[cn].L++;
		else
		    b.nodes[cn].R++;	

		//cout << cn << "\t" << ec.final_prediction << "\t" << b.nodes[cn].R  << "\t" << b.nodes[cn].L << endl;

		b.ec_path.pop();
		update_min_ec_count(b, &b.ec_path);	
	       	
		ec.final_prediction = tmp_final_prediction;	
		ec.ld = mc;
		
		break;	
	      }
	   
	    //cout << ec.final_prediction <<endl;
	    if(ec.final_prediction < 0)//b.nodes[cn].Eh/b.nodes[cn].n)
	      {
		b.nodes[cn].L++;
		cn = b.nodes[cn].id_left;
	      }	
	    else
	      {
		b.nodes[cn].R++;
		cn = b.nodes[cn].id_right;	
	      }
	  }	
      }
      b.ex_num++;
      //cout << endl;
  }
  
  void finish(txm_o& b)
  {
    //display_tree2(b);
    //save_node_stats(b);
    //fclose(b.ex_fp);
  }
  
  void save_load_tree(txm_o& b, io_buf& model_file, bool read, bool text)
  {
    if (model_file.files.size() > 0)
      {	
	char buff[512];
	uint32_t i = 0;
	uint32_t j = 0;
	size_t brw = 1;
	uint32_t v;
	int text_len;
	
	if(read)
	  { 
	    brw = bin_read_fixed(model_file, (char*)&i, sizeof(i), "");
	    
	    for(j = 0; j < i; j++)
	      {	
		//b.nodes.push_back(init_node(0));
		b.nodes.push_back(init_node());
		
		brw +=bin_read_fixed(model_file, (char*)&v, sizeof(v), "");
		b.nodes[j].id_left = v;
		brw +=bin_read_fixed(model_file, (char*)&v, sizeof(v), "");
		b.nodes[j].id_right = v;
		brw +=bin_read_fixed(model_file, (char*)&v, sizeof(v), "");
		b.nodes[j].max_cnt2_label = v;
		brw +=bin_read_fixed(model_file, (char*)&v, sizeof(v), "");
		b.nodes[j].leaf = v;
	      }
      	    
	    /*update_depth(b);
	    cout << endl << endl;
	    cout << "Tree depth: " << b.max_depth << endl;
	    cout << "Average tree depth: " << b.avg_depth << endl;
	    cout << "ceil of log2(k): " << ceil_log2(b.k) << endl;
	    cout << "Number of swaps: " << b.nbofswaps << endl << endl;*/
	  }
	else
	  {    
	    text_len = sprintf(buff, ":%d\n", (int) b.nodes.size());	//ilosc nodow
	    v = b.nodes.size();
	    brw = bin_text_write_fixed(model_file,(char *)&v, sizeof (v), buff, text_len, text);
	    
	    for(i = 0; i < b.nodes.size(); i++)
	      {	
		text_len = sprintf(buff, ":%d", (int) b.nodes[i].id_left);
		v = b.nodes[i].id_left;
		brw = bin_text_write_fixed(model_file,(char *)&v, sizeof (v), buff, text_len, text);
		
		text_len = sprintf(buff, ":%d", (int) b.nodes[i].id_right);
		v = b.nodes[i].id_right;
		brw = bin_text_write_fixed(model_file,(char *)&v, sizeof (v), buff, text_len, text);
		
		text_len = sprintf(buff, ":%d", (int) b.nodes[i].max_cnt2_label);
		v = b.nodes[i].max_cnt2_label;
		brw = bin_text_write_fixed(model_file,(char *)&v, sizeof (v), buff, text_len, text);	
		
		text_len = sprintf(buff, ":%d\n", b.nodes[i].leaf);
		v = b.nodes[i].leaf;
		brw = bin_text_write_fixed(model_file,(char *)&v, sizeof (v), buff, text_len, text);	
	      }	
	  }	
      }	
  }	
  
  void finish_example(vw& all, txm_o&, example& ec)
  {
    MULTICLASS::output_example(all, ec);
    VW::finish_example(all, &ec);
  }
  /*
  learner* setup(vw& all, po::variables_map& vm)	//learner setup
  {
    txm_o* data = (txm_o*)calloc(1, sizeof(txm_o));
    
    po::options_description txm_o_opts("TXM Online options");
    txm_o_opts.add_options()
      ("no_progress", "disable progressive validation");
    
    vm = add_options(all, txm_o_opts);
    
    data->k = (uint32_t)vm["txm_o"].as<size_t>();
    
    //append txm_o with nb_actions to options_from_file so it is saved to regressor later
    std::stringstream ss;
    ss << " --txm_o " << data->k;
    all.file_options.append(ss.str());
    
    if (vm.count("no_progress"))
      data->progress = false;
    else
      data->progress = true;
    
    data->all = &all;
    (all.p->lp) = MULTICLASS::mc_label;
    
    string loss_function = "quantile";
    float loss_parameter = 0.5;
    delete(all.loss);
    all.loss = getLossFunction(&all, loss_function, loss_parameter);
    
    uint32_t i = ceil_log2(data->k);	
    data->max_nodes = (2 << (i+0)) - 1;
    
    learner* l = new learner(data, all.l, data->max_nodes + 1);
    l->set_save_load<txm_o,save_load_tree>();
    l->set_learn<txm_o,learn>();
    l->set_predict<txm_o,predict>();
    l->set_finish_example<txm_o,finish_example>();
    l->set_finish<txm_o,finish>();
    
    if(all.training)
      init_tree(*data);	
    
    return l;
  }	*/

  learner* setup(vw& all, std::vector<std::string>&opts, po::variables_map& vm, po::variables_map& vm_file)	//learner setup
  {
    txm_o* data = (txm_o*)calloc(1, sizeof(txm_o));

    //po::options_description desc("TXM_O options");
    //desc.add_options()
    //("txm_o_depth", po::value<int>(), "maximum depth past log k");

    //po::parsed_options parsed = po::command_line_parser(opts).
    //style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing).
    //options(desc).allow_unregistered().run();
    //opts = po::collect_unrecognized(parsed.options, po::include_positional);
    //po::store(parsed, vm);
    //po::notify(vm);

    //first parse for number of actions
    if( vm_file.count("txm_o") )
      {
	data->k = (uint32_t)vm_file["txm_o"].as<size_t>();
	if( vm.count("txm_o") && (uint32_t)vm["txm_o"].as<size_t>() != data->k )
	  std::cerr << "warning: you specified a different number of actions through --txm_o than the one loaded from predictor. Pursuing with loaded value of: " << data->k << endl;
      }
    else
      {
	data->k = (uint32_t)vm["txm_o"].as<size_t>();
	
	//append txm_o with nb_actions to options_from_file so it is saved to regressor later
	std::stringstream ss;
	ss << " --txm_o " << data->k;
	all.options_from_file.append(ss.str());
      }	
    
    //int depth = 0;
    //if (vm.count("txm_o_depth"))
    //depth = vm["txm_o_depth"].as<int>();

    data->all = &all;
    (all.p->lp) = MULTICLASS::mc_label_parser;
    
    uint32_t i = ceil_log2(data->k);	
    data->max_nodes = (2 << (i+0)) - 1;
    
    learner* l = new learner(data, all.l, data->max_nodes + 1);
    l->set_save_load<txm_o,save_load_tree>();
    l->set_learn<txm_o,learn>();
    l->set_predict<txm_o,predict>();
    l->set_finish_example<txm_o,finish_example>();
    l->set_finish<txm_o,finish>();
    
    //data->max_depth = 0;
    
    if(all.training)
      init_tree(*data);	
    
    return l;
    }
}

