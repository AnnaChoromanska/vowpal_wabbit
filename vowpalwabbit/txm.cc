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
#include "gd.h"

using namespace std;
using namespace LEARNER;

namespace TXM
{
  uint32_t ceil_log2(uint32_t k)
  {
    uint32_t i = 0;
    
    while (k > (uint32_t)(1 << i))
      i++;
    
    return i;
  }
  
  class txm_node_pred_type	
  {
  public:
    
    uint32_t Rk;	
    double Ehk;	
    float norm_Ehk;
    uint32_t nk;
    uint32_t label;	
    uint32_t label_cnt2;
    uint32_t example_counter;
    
    bool operator==(txm_node_pred_type v){
      return (label == v.label);
    }
    
    bool operator>(txm_node_pred_type v){
      if(label > v.label) return true;	
      return false;
    }
    
    bool operator<(txm_node_pred_type v){
      if(label < v.label) return true;	
      return false;
    }
    
    txm_node_pred_type(uint32_t l)
    {
      label = l;
      Ehk = 0.f;
      norm_Ehk = 0;
      nk = 0;
      label_cnt2 = 0;
      Rk = 0;	
    }
  };
  
  typedef struct
  {
    size_t id_left;
    size_t id_right;
    size_t level;
    size_t max_cnt2;
    size_t max_cnt2_label;
    int8_t initial_dir;	
    
    uint32_t ec_count;
    uint32_t L;
    uint32_t R;
    float objective;

    uint32_t myL;
    uint32_t myR;
    bool leaf;
    v_array<txm_node_pred_type> node_pred;
    
    double Eh;	
    float norm_Eh;
    uint32_t n;	
    v_array<double> means;
    size_t means_cnt;
  } txm_node_type;
  
  struct txm	
  {
    uint32_t k;	
    vw* all;	
    
    v_array<txm_node_type> nodes;	
    
    size_t max_depth;	
    size_t max_nodes;

    size_t passes_per_level; //number of passes per level	
    size_t ctl; //current training level
    size_t ex_num;
    bool check;
    size_t cn;
    example ec;
    FILE *ex_fp;
  };	
  
  txm_node_type init_node(size_t level)	
  {
    txm_node_type node; 
    
    node.id_left = 0;
    node.id_right = 0;
    node.Eh = 0;
    node.norm_Eh = 0;
    node.n = 0;
    node.level = level;
    node.leaf = true;
    node.max_cnt2 = 0;
    node.max_cnt2_label = 0;
    node.initial_dir = 0;
    node.ec_count = 0;
    node.L = 0;
    node.R = 0;
    node.objective = 0;
    node.myL = 0;
    node.myR = 0;
    node.means_cnt = 0;
    return node;
  }
  
  void init_tree(txm& d)
  {
    d.nodes.push_back(init_node(0));
    d.ex_num = 0;
    d.ctl = 0;
    //d.ex_fp = fopen("ex_nums.txt", "wt");
  }
  
  void add_mean_to_features(vw& all, example& ec, txm& b)
  {
    size_t index = 0;
    double tmp;
    
    for (unsigned char* i = ec.indices.begin; i != ec.indices.end; i++) 
      {
        size_t original_length = ec.atomics[*i].size();
        for (uint32_t j = 0; j < original_length; j++)
          {
            feature* f = &ec.atomics[*i][j];
            if(index < b.nodes[b.cn].means.size())
	    {
		tmp = b.nodes[b.cn].means[index++];
		tmp /= b.nodes[b.cn].means_cnt;
		f->x = f->x - (float)tmp;
	    }
          }
      }
  }
  
  inline void update_mean(txm& b, float x, float& fw, size_t index)
  {
    if(index == 0)
    {
      if(b.check)
	return;
      else
        b.check = true;
    }
    
    if(index >= b.nodes[b.cn].means.size())
      b.nodes[b.cn].means.push_back(x);
    else
      b.nodes[b.cn].means[index] += x;
    //cout << index << ":" << x << ":" << b.means[index] <<"\t";
  }   

  float print_intercept(vw& all, example& ec, learner& base, size_t& cn)
  {
    float w_1 = 0.;
    float w_0 = 0.;
					       
    bool got_first = true;

    ec.ft_offset += (uint32_t)(base.increment*cn);
    for (unsigned char* i = ec.indices.begin; i != ec.indices.end; i++) 
      if (got_first)
	{
	  w_1 = all.reg.weight_vector[((ec.atomics[*i].begin)->weight_index + ec.ft_offset) & all.reg.weight_mask];
	  got_first = false;
	}
      else
	w_0 = all.reg.weight_vector[((ec.atomics[*i].begin)->weight_index + ec.ft_offset) & all.reg.weight_mask];
    ec.ft_offset -= (uint32_t)(base.increment*cn);

    float w_ratio = -w_0/w_1;

    return w_ratio;
  }
  
  void train_node(txm& b, learner& base, example& ec, size_t& cn, size_t& index)
  {
    label_data* simple_temp = (label_data*)ec.ld;
    
    b.cn = cn;
    VW::copy_example_data(b.all->audit, &b.ec, &ec);    
    if(b.cn > 0)
	add_mean_to_features(*b.all, b.ec, b);    
    b.ec.ld = ec.ld;
    
    if(b.nodes[cn].level != b.ctl)
    {
	simple_temp->label = FLT_MAX;
	base.predict(b.ec, cn);
	ec.final_prediction = b.ec.final_prediction;
	ec.partial_prediction = b.ec.partial_prediction;  
	return;    
    }   
       
    b.nodes[cn].objective = 0;
    float tmp1, tmp2, tmp3;
    for(size_t i = 0; i < b.nodes[cn].node_pred.size(); i++)
      {
	tmp1 = (float)b.nodes[cn].R / (float)b.nodes[cn].n;
	tmp2 = (float)b.nodes[cn].node_pred[i].Rk / (float)b.nodes[cn].node_pred[i].nk;
	tmp3 = (float)b.nodes[cn].node_pred[i].nk / (float)b.nodes[cn].n;
	b.nodes[cn].objective += tmp3 * fabs(tmp1 - tmp2);
      }
    
    float left_or_right = b.nodes[cn].node_pred[index].norm_Ehk - b.nodes[cn].norm_Eh;
    
    size_t id_left = b.nodes[cn].id_left;
    size_t id_right = b.nodes[cn].id_right;
    
    size_t id_left_right;	
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
    /*  
    if(cn == 1)
      {
	cout << b.nodes[cn].node_pred[index].label << "\t" << simple_temp->label;
      }
    */    
    if((b.nodes[cn].myR > 0) && (b.nodes[cn].myL > 0))
      {
	if(id_left_right == 0)
	  {	    
	    if(b.nodes.size() + 2 <= b.max_nodes)
	      {
		id_left_right = b.nodes.size();	
		b.nodes.push_back(init_node(b.nodes[cn].level + 1));	
		b.nodes.push_back(init_node(b.nodes[cn].level + 1));
		b.nodes[cn].id_left = id_left_right;
		b.nodes[cn].id_right = id_left_right + 1;
		
		b.nodes[b.nodes[cn].id_left].max_cnt2_label = b.nodes[cn].max_cnt2_label;
		b.nodes[b.nodes[cn].id_right].max_cnt2_label = b.nodes[cn].max_cnt2_label;
		
		if(b.nodes[cn].level + 1 > b.max_depth)
		  {
		    b.max_depth = b.nodes[cn].level + 1;	
		  }	
	      }
	  }	
      }
    base.learn(b.ec, cn);	

    simple_temp->label = FLT_MAX;
    base.predict(b.ec, cn);
        
    b.nodes[cn].Eh += (double)b.ec.partial_prediction;
    b.nodes[cn].node_pred[index].Ehk += (double)b.ec.partial_prediction;
    b.nodes[cn].n++;
    b.nodes[cn].node_pred[index].nk++;	
  
    b.nodes[cn].norm_Eh = b.nodes[cn].Eh / b.nodes[cn].n;          
    b.nodes[cn].node_pred[index].norm_Ehk = b.nodes[cn].node_pred[index].Ehk / b.nodes[cn].node_pred[index].nk;
    
    /*if(cn == 1)
	{
	  cout << "\t" << ec.partial_prediction << "\t" << ec.final_prediction<< "\t" << print_intercept(*b.all, ec, base, cn) << endl;
	  cin.ignore();
	  }*/
	
	/*if(cn == 1)
      {
	cout << b.nodes[cn].node_pred[index].label << "\t" << b.nodes[cn].norm_Eh << "\t" << b.nodes[cn].node_pred[index].norm_Ehk  << "\t" << left_or_right << "\t" << simple_temp->label << "\t" <<  ec.final_prediction << "\t" << ec.partial_prediction << "\t" << print_intercept(*b.all, ec) << endl;
	//cin.ignore();
      }*/
    
    if(b.nodes[cn].id_left == 0)	
      b.nodes[cn].leaf = true;
    else	
      b.nodes[cn].leaf = false;	
      
    ec.final_prediction = b.ec.final_prediction;
    ec.partial_prediction = b.ec.partial_prediction;  
  }
  
  void predict(txm& b, learner& base, example& ec)	
  {
    MULTICLASS::mc_label *mc = (MULTICLASS::mc_label*)ec.ld;
     
    label_data simple_temp;
    simple_temp.initial = 0.0;
    simple_temp.weight = mc->weight;	
    b.ec.ld = &simple_temp;
    size_t cn = 0;
    while(1)
      {	
	b.cn = cn;
	VW::copy_example_data(b.all->audit, &b.ec, &ec);    
        if(b.cn > 0)
	  add_mean_to_features(*b.all, b.ec, b);    
        b.ec.ld = ec.ld;    
	
	if(b.nodes[cn].leaf)	
	  {	
	    ec.final_prediction = b.nodes[cn].max_cnt2_label;
	    ec.ld = mc;
	    break;	
	  }
	simple_temp.label = FLT_MAX;
	base.predict(b.ec, cn);
  
	if(b.ec.final_prediction < 0)//b.nodes[cn].norm_Eh)	
	  cn = b.nodes[cn].id_left;
	else
	  cn = b.nodes[cn].id_right;	
      }	
  }
    
  void display_tree2(txm& d)
  {
    size_t l, i;
    
    if(d.k < 200)
    {
	    for(l = 0; l <= d.max_depth; l++)
	      {
		for(i = 0; i < d.nodes.size(); i++)
		  {
		    if(d.nodes[i].level == l)
		      {	
			if(d.nodes[i].leaf)
			  cout << "[" << i << "," << d.nodes[i].max_cnt2_label << "," << d.nodes[i].max_cnt2 << "," << d.nodes[i].ec_count << "," << d.nodes[i].objective << "] ";
			else
			  cout << "(" << i << "," << d.nodes[i].max_cnt2_label << "," << d.nodes[i].max_cnt2 << "," << d.nodes[i].ec_count << "," << d.nodes[i].objective << ") ";
		      }
		  }
		cout << endl;
	      }
	    cout << endl;
    }
    
    cout << endl;
    cout << "Tree depth: " << d.max_depth << endl;
    cout << "ceil of log2(k): " << ceil_log2(d.k) << endl;
  }
  
  void display_tree3(txm& d)
  {
    if(d.k >= 200)
	return;
	
    size_t l, i, j;
        
    for(l = 0; l <= d.max_depth; l++)
      {
	for(i = 0; i < d.nodes.size(); i++)
	  {
	    if(d.nodes[i].level == l)
	      {	
		if(d.nodes[i].leaf) 
		{
		  cout << "[";
		  for(j = 0; j < d.nodes[i].means.size(); j++) 
		    cout << d.nodes[i].means[j] / d.nodes[i].means_cnt << ",";
		  cout << "] ";
		}
		else
		{
		 cout << "(";
		  for(j = 0; j < d.nodes[i].means.size(); j++) 
		    cout << d.nodes[i].means[j] / d.nodes[i].means_cnt << ",";
		 cout << ") ";
		}
	      }
	  }
	cout << endl;
      }
    cout << endl;
    
    cout << endl;
    cout << "Tree depth: " << d.max_depth << endl;
    cout << "ceil of log2(k): " << ceil_log2(d.k) << endl;
  }
  
  void save_node_stats(txm& d)
  {
    FILE *fp;
    uint32_t i, j;
    size_t total;
    txm* b = &d;
    
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
  }	
  
  void learn(txm& b, learner& base, example& ec)
  {
    predict(b,base,ec);
    MULTICLASS::mc_label *mc = (MULTICLASS::mc_label*)ec.ld;
    
    if(b.ctl != b.all->current_pass / b.passes_per_level)
    {
	b.ctl = b.all->current_pass / b.passes_per_level;
	cout << "ctl: " << b.ctl << endl;
    }

    if(b.all->training && (mc->label != (uint32_t)-1) && !ec.test_only)	//if training the tree
      {
	size_t index = 0;
	
	label_data simple_temp;	
	simple_temp.initial = 0.0;
	simple_temp.weight = mc->weight;
	ec.ld = &simple_temp;	
	uint32_t oryginal_label = mc->label;	
		
	size_t tmp_final_prediction = ec.final_prediction;
	size_t cn = 0;

	while(1)
	  {
	    if(b.nodes[cn].level == b.ctl)
	      b.nodes[cn].ec_count++;

	    index = b.nodes[cn].node_pred.unique_add_sorted(txm_node_pred_type(oryginal_label));
	    
	    b.nodes[cn].node_pred[index].label_cnt2++;
	    
	    if(b.nodes[cn].node_pred[index].label_cnt2 > b.nodes[cn].max_cnt2)
	      {
		b.nodes[cn].max_cnt2 = b.nodes[cn].node_pred[index].label_cnt2;
		b.nodes[cn].max_cnt2_label = b.nodes[cn].node_pred[index].label;
	      }
	    
	    train_node(b, base, ec, cn, index);
	    
	    if(b.nodes[cn].leaf)
	      {	
		ec.final_prediction = tmp_final_prediction;	
		ec.ld = mc;	
		
		break;	
	      }
	    
	    if(ec.final_prediction < 0)//b.nodes[cn].norm_Eh)
		{

		  b.nodes[cn].L++;
		  cn = b.nodes[cn].id_left;
		}	
	    else
	      {
		b.nodes[cn].R++;
		b.nodes[cn].node_pred[index].Rk++;
		cn = b.nodes[cn].id_right;	
	      }
	      
	      if(b.nodes[cn].level == b.ctl + 1)
	      {
		 b.check = false;
		 b.cn = cn;
		 GD::foreach_feature<txm, update_mean>(*b.all, ec, b);
		 b.nodes[cn].means_cnt++;
	      }
	  }	
      }
      b.ex_num++;
  }
  
  void finish(txm& b)
  {
    display_tree2(b);
    //display_tree3(b);
    save_node_stats(b);
    //fclose(b.ex_fp);
  }
  
  void save_load_tree(txm& b, io_buf& model_file, bool read, bool text)
  {
    if (model_file.files.size() > 0)
      {	
	char buff[512];
	uint32_t i = 0;
	uint32_t j = 0;
	size_t brw = 1;
	uint32_t v;
	double vd;
	uint32_t tmp;
	int text_len;
	uint32_t k;
	
	if(read)
	  {
	    brw = bin_read_fixed(model_file, (char*)&i, sizeof(i), "");
	    
	    for(j = 0; j < i; j++)
	      {	
		b.nodes.push_back(init_node(0));
		
		brw +=bin_read_fixed(model_file, (char*)&v, sizeof(v), "");
		b.nodes[j].id_left = v;
		brw +=bin_read_fixed(model_file, (char*)&v, sizeof(v), "");
		b.nodes[j].id_right = v;
		brw +=bin_read_fixed(model_file, (char*)&v, sizeof(v), "");
		b.nodes[j].max_cnt2_label = v;
		brw +=bin_read_fixed(model_file, (char*)&v, sizeof(v), "");
		b.nodes[j].leaf = v;
		brw +=bin_read_fixed(model_file, (char*)&v, sizeof(v), "");
		b.nodes[j].means_cnt = v;
		brw +=bin_read_fixed(model_file, (char*)&v, sizeof(v), "");
		tmp = v;
		for(k = 0; k < tmp; k++)
		{
		  brw +=bin_read_fixed(model_file, (char*)&vd, sizeof(vd), "");
		  b.nodes[j].means.push_back(vd);
		}
	      }
	  }
	else
	  {
	    //cout << endl;
	    //cout << "Tree depth: " << b.max_depth << endl;
	    //cout << "ceil of log2(k): " << ceil_log2(b.k) << endl;
	    
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
		
		text_len = sprintf(buff, ":%d\n", b.nodes[i].means_cnt);
		v = b.nodes[i].means_cnt;
		brw = bin_text_write_fixed(model_file,(char *)&v, sizeof (v), buff, text_len, text);	
		
		text_len = sprintf(buff, ":%d\n", b.nodes[i].means_cnt);
		v = b.nodes[i].means.size();
		brw = bin_text_write_fixed(model_file,(char *)&v, sizeof (v), buff, text_len, text);	
		
		for(k = 0; k < b.nodes[i].means.size(); k++)
		{
		  text_len = sprintf(buff, ":%d\n", b.nodes[i].means[k]);
		  vd = b.nodes[i].means[k];
		  brw = bin_text_write_fixed(model_file,(char *)&vd, sizeof (vd), buff, text_len, text);	
		}
	      }	
	  }	
      }	
  }	
  
  void finish_example(vw& all, txm&, example& ec)
  {
    MULTICLASS::output_example(all, ec);
    VW::finish_example(all, &ec);
  }
  
  learner* setup(vw& all, std::vector<std::string>&opts, po::variables_map& vm, po::variables_map& vm_file)	//learner setup
  {
    txm* data = (txm*)calloc(1, sizeof(txm));
    //first parse for number of actions
    if( vm_file.count("txm") )
      {
	data->k = (uint32_t)vm_file["txm"].as<size_t>();
	if( vm.count("txm") && (uint32_t)vm["txm"].as<size_t>() != data->k )
	  std::cerr << "warning: you specified a different number of actions through --txm than the one loaded from predictor. Pursuing with loaded value of: " << data->k << endl;
      }
    else
      {
	data->k = (uint32_t)vm["txm"].as<size_t>();
	
	//append txm with nb_actions to options_from_file so it is saved to regressor later
	std::stringstream ss;
	ss << " --txm " << data->k;
	all.options_from_file.append(ss.str());
      }	
    
    data->all = &all;
    (all.p->lp) = MULTICLASS::mc_label_parser;
    
    uint32_t i = ceil_log2(data->k);	
    data->max_nodes = (2 << i) - 1;
    
    learner* l = new learner(data, all.l, data->max_nodes + 1);
    l->set_save_load<txm,save_load_tree>();
    l->set_learn<txm,learn>();
    l->set_predict<txm,predict>();
    l->set_finish_example<txm,finish_example>();
    l->set_finish<txm,finish>();
    
    data->max_depth = 0;
    data->passes_per_level = 2;
    
    if(all.training)
      init_tree(*data);	
    
    return l;
  }	
}

