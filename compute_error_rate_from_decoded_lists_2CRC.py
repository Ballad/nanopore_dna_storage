import helper
import os 

# PARAMETERS TO BE SET BEFORE RUNNING THE CODE
LIST_SIZE = 8
DECODED_LISTS_DIR = '/raid/nanopore/shubham/20200804_nanopore_pool_data/data/decoded_lists/exp_17_bonito_default_bcp_0.6/'
CONV_INPUT_FILE = '/raid/nanopore/shubham/20200214_nanopore_pool_data/nanopore_dna_storage/oligos_8_4_20/reads.17.conv_input'
pad = False
bytes_per_oligo = 20

num_RS_segments = bytes_per_oligo//2
true_RS_segments_list = [{} for i in range(num_RS_segments)]
# map from index to [[true_payload_bytes]]

print('list size:',LIST_SIZE)
with open(CONV_INPUT_FILE) as f:
    conv_input_list = [s.rstrip('\n') for s in f.readlines()]
        
num_oligos = len(conv_input_list)
print('num_oligos',num_oligos)

for i,conv_input in enumerate(conv_input_list):
    output_list = helper.decode_list_2CRC_index([conv_input],bytes_per_oligo,num_oligos,pad)
    # the decode function is meant for decoding, but we use it here to get the true thing
    for (index,pos,true_segment) in output_list:
        true_RS_segments_list[pos][index] = true_segment

num_reads = 0
num_correct = [0 for _ in range(num_RS_segments)]
num_erasure_CRC_index = [0 for _ in range(num_RS_segments)]
num_error_CRC_index = [0 for _ in range(num_RS_segments)]

for filename in os.listdir(DECODED_LISTS_DIR):
    if not filename.startswith("list_"):
        continue
    num_reads += 1
    with open(os.path.join(DECODED_LISTS_DIR,filename)) as f:
        decoded_msg_list = [l.rstrip('\n') for l in f.readlines()]
        decoded_msg_list = decoded_msg_list[:LIST_SIZE]
    output_list = helper.decode_list_2CRC_index(decoded_msg_list,bytes_per_oligo,num_oligos,pad)
    for (index,pos,segment) in output_list:
        if segment == true_RS_segments_list[pos][index]:
            num_correct[pos] += 1
        else:
            num_error_CRC_index[pos] += 1

for i in range(num_RS_segments):
    num_erasure_CRC_index[i] = num_reads - num_correct[i]
            
print('num_reads:',num_reads)
for i in range(num_RS_segments):
    print('------------------------------------')
    print('RS segment: '+str(i))
    print('num_correct:',num_correct[i])
    print('num_erasure_CRC_index:',num_erasure_CRC_index[i])
    print('num_error_CRC_index:',num_error_CRC_index[i])
