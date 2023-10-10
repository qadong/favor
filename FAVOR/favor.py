from __future__ import absolute_import, division, print_function
import argparse
import logging
import os
import random
import numpy as np
import torch
import torch.nn.functional as F
import re
import json
from torch import nn
from torch.utils.data import DataLoader, Dataset, SequentialSampler, RandomSampler
from transformers import (AdamW, get_linear_schedule_with_warmup,
                          T5ForConditionalGeneration, AutoTokenizer, AutoConfig)
from tqdm import tqdm
import pandas as pd
from torch.utils.tensorboard import SummaryWriter
import datasets
from transformers import GPT2LMHeadModel, AutoTokenizer, RobertaModel


def clean_tokens(tokens):
    tokens = tokens.replace("<pad>", "")
    tokens = tokens.replace("<s>", "")
    tokens = tokens.replace("</s>", "")
    tokens = tokens.replace("<Path_Start>", "")
    tokens = tokens.replace("<Path_End>", "")
    tokens = tokens.replace("<BUg_Line>", "")
    tokens = tokens.replace("<Line>", "")
    tokens = tokens.strip("\n")
    tokens = tokens.strip()
    return tokens


def add_special_tokens(path, bug_locs):
    for bug_loc in bug_locs:
        path = path.replace('line {}'.format(bug_loc), '<Bug_Line> {}'.format(bug_loc))
    path = path.replace('Start\n', '<Path_Start>')
    path = path.replace('\nEnd\n\n', '<Path_End>')
    path = path.replace('\n line', ' <line>')
    return path


def main():
    parser = argparse.ArgumentParser()
    # Params
    parser.add_argument("--encoder_block_size", default=-1, type=int,
                        help="Optional input sequence length after tokenization."
                             "The training dataset will be truncated in block of this size for training."
                             "Default to the model max input length for single sentence inputs (take into account special tokens).")
    parser.add_argument("--decoder_block_size", default=-1, type=int,
                        help="Optional input sequence length after tokenization."
                             "The training dataset will be truncated in block of this size for training."
                             "Default to the model max input length for single sentence inputs (take into account special tokens).")
    parser.add_argument("--num_beams", default=50, type=int,
                        help="Beam size to use when decoding.")
    parser.add_argument("--model_name", default="model.bin", type=str,
                        help="Saved model name.")
    parser.add_argument("--model_name_or_path", default=None, type=str,
                        help="The model checkpoint for weights initialization.")
    parser.add_argument("--tokenizer_name", default="", type=str,
                        help="Optional pretrained tokenizer name or path if not the same as model_name_or_path")

    args = parser.parse_args()
    logger = logging.getLogger(__name__)
    # Setup CUDA, GPU
    device = torch.device("cuda:0" if torch.cuda.is_available() else "cpu")
    args.n_gpu = 1
    args.device = device

    # Setup logging
    logging.basicConfig(format='%(asctime)s - %(levelname)s - %(name)s -   %(message)s', datefmt='%m/%d/%Y %H:%M:%S',
                        level=logging.INFO)
    logger.warning("device: %s, n_gpu: %s", device, args.n_gpu, )
    # Set seed
    tokenizer = AutoTokenizer.from_pretrained(args.tokenizer_name)
    tokenizer.add_tokens(
        ["<Path_Start>", "<Path_End>", "<S2SV_blank>", "<S2SV_ModStart>", "<S2SV_ModEnd>", "<Bug_Line>", "<Line>", "<S2SV_blank>", "<S2SV_null>"])
    model = T5ForConditionalGeneration.from_pretrained(args.model_name_or_path)
    model.resize_token_embeddings(len(tokenizer))
    logger.info("Testing parameters %s", args)

    checkpoint_prefix = f'./{args.model_name}'
    model.load_state_dict(torch.load(checkpoint_prefix, map_location=args.device))
    model.to(args.device)

    with open("paths.json", "r") as f:
        data = json.load(f)
    path_no = data.get("path_no", 0)
    input_data = [data[f"path{i + 1}"] for i in range(path_no)]
    bug_locs = data.get("bug_locs", [])
    model.eval()
    logger.info("*************Running testing************")
    patch_data = {}
    for i, inp in enumerate(input_data):
        path = add_special_tokens(inp, bug_locs)
        logger.info("****path {}:{} \n".format(i, path))
        input_ids = tokenizer.encode(path, truncation=True, max_length=args.encoder_block_size, padding='max_length', return_tensors='pt').to(args.device)
        attention_mask = input_ids.ne(0).to(args.device)
        with torch.no_grad():
            beam_output = model.generate(input_ids=input_ids,
                                          attention_mask=attention_mask,
                                          do_sample=False,
                                          num_beams=20,
                                          num_return_sequences=1,
                                          max_length=args.decoder_block_size)
        beam_output = beam_output.detach().cpu().tolist()
        prediction = tokenizer.decode(beam_output[0], skip_special_tokens=False)
        prediction = clean_tokens(prediction)
        logger.info("****patch {}:{} \n".format(i,prediction))
        patch_data[f'patch {i+1}'] = prediction
    with open('patches.json','w') as f:
        json.dump(patch_data, f, indent=4)

    logger.info("*************predictions complete**************")


if __name__ == "__main__":
    main()
