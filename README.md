Preload the heapmon.so on the execution line of the program

    LD_PRELOAD=/absolute/path/to/heapmon.so foobar

Let the program get to the spot where you want to "mark" the heap.

Touch the control file

    touch heapmon.ctl

Let the program run over the desired period.

Touch the control file again

    touch heapmon.ctl

Evaluate heapmon.log
