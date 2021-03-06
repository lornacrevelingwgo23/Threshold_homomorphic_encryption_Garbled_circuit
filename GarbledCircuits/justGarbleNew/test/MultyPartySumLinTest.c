/*
 This file is part of JustGarble.

 JustGarble is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 JustGarble is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with JustGarble.  If not, see <http://www.gnu.org/licenses/>.

 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include "../include/justGarble.h"
#include "../gwas/genoreader.c"
#include "../gwas/arith.c"


int main() {

	char* refId = "rs55725292";
	int phenotypeId = 1;
	int ancestryId = 1;

	// CI part

    int i,j;
    read_names();
    read_ids();

    int size_names = size_of_names();
    int size_ids = size_of_ids();

    int refId_id = find_ids_id(refId);


    read_encs();
    char encC[size_names];
    for (i= 0; i< size_names;i++) {
    	encC[i] = get_char_in_enc((size_names*refId_id)+i);
    }
    read_skeys();
    char skeyC[size_names];
    for (i= 0; i< size_names;i++) {
    	skeyC[i] = get_char_in_skeys((size_names*refId_id)+i);
    }
    read_ancs();
    char ancC[size_names];
    for (i= 0; i< size_names;i++) {
    	ancC[i] = get_char_in_anc((size_names*ancestryId)+i);
    }
    read_ancSkeys();
    char ancskeyC[size_names];
    for (i= 0; i< size_names;i++) {
    	ancskeyC[i] = get_char_in_ancSkeys((size_names*ancestryId)+i);
    }
    read_phens();
    char phenC[size_names];
    for (i= 0; i< size_names;i++) {
    	phenC[i] = get_char_in_phens((size_names*phenotypeId)+i);
    }
    read_skeys();
    char phenskeyC[size_names];
    for (i= 0; i< size_names;i++) {
    	phenskeyC[i] = get_char_in_phensSkeys((size_names*phenotypeId)+i);
    }

    // SPU -> GC building part

	GarbledCircuit garbledCircuit;
	GarblingContext garblingContext;

	int inputsNb = 32*size_names*3;
	int wiresNb = 100000000;
	int gatesNb = 100000000;
	int outputsNb = 32*4;

	//Create a circuit.
	block labels[2 * inputsNb];
	createInputLabels(labels, inputsNb);
	InputLabels inputLabels = labels;
	createEmptyGarbledCircuit(&garbledCircuit, inputsNb, outputsNb, gatesNb,
			wiresNb, inputLabels);
	startBuilding(&garbledCircuit, &garblingContext);

	// Transform generator's input into fixed wire
	int zero = fixedZeroWire(&garbledCircuit,&garblingContext);
	int one = fixedOneWire(&garbledCircuit,&garblingContext);

	int onewire = getNextWire(&garblingContext);
	NOTGate(&garbledCircuit,&garblingContext,zero,onewire);

	int zerowire = getNextWire(&garblingContext);
	NOTGate(&garbledCircuit,&garblingContext,one,zerowire);

	int outputs[outputsNb];
	int *inp = (int *) malloc(sizeof(int) * inputsNb*2);
	countToN(inp, inputsNb);
	countToN(outputs, outputsNb);

	int bits[inputsNb];
	chars_to_ints(encC,size_names,bits);
	chars_to_ints(ancC,size_names,bits+32*size_names);
	chars_to_ints(phenC,size_names,bits+64*size_names);

	for (i = 0; i < inputsNb; i++) {
		if (bits[i]) {
			inp[inputsNb+i] = onewire;
		} else {
			inp[inputsNb+i] = zerowire;
		}
	}
	int tempOutPut[inputsNb];
	XORCircuit(&garbledCircuit, &garblingContext, inputsNb*2, inp, tempOutPut);

	sumLin(&garbledCircuit, &garblingContext, tempOutPut, tempOutPut + (size_names * 32), tempOutPut + (2 * size_names * 32), size_names, 32, zerowire, outputs);

	block *outputbs = (block*) malloc(sizeof(block) * outputsNb);
	OutputMap outputMap = outputbs;
	finishBuilding(&garbledCircuit, &garblingContext, outputMap, outputs);
	garbleCircuit(&garbledCircuit, inputLabels, outputMap);

	// MU -> Evaluation part
	block extractedLabels[inputsNb];

	int extractedInputs[inputsNb];
	chars_to_ints(skeyC,size_names,extractedInputs);
	chars_to_ints(ancskeyC,size_names,extractedInputs+32*size_names);
	chars_to_ints(phenskeyC,size_names,extractedInputs+64*size_names);

	extractLabels(extractedLabels, inputLabels, extractedInputs, inputsNb);
	block computedOutputMap[outputsNb];
	evaluate(&garbledCircuit, extractedLabels, computedOutputMap);
	int outputVals[outputsNb];
	mapOutputs(outputMap, computedOutputMap, outputVals, outputsNb);
	//TODO
	int res = ints_into_int(outputVals);
	printf("RESULT IS : %d\n",res);
	return 0;
}

