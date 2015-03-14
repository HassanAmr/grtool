#!/usr/bin/env python
# -*- coding: utf-8 -*-

import matplotlib as mp, os, sys, argparse, fileinput, numpy as np, matplotlib.animation as animation
from matplotlib import pyplot as plt
from threading import Thread
from time import time,sleep
from queue import Queue
from inspect import isclass
from math import ceil

class ScatterPlot:
    def __init__(self,labels,data):
        ndim = len(data[0])
        self.x,self.y = ndim,ndim
        self.sc = [None] * ndim*ndim
        plt.subplot(self.x,self.y,1)
        self(0,labels,data)

    def __call__(self,frameno,labels,data):
        self.labels = list(set(labels))
        self.cm     = mp.cm.get_cmap("cubehelix", len(self.labels)+1)

        markers = list(mp.markers.MarkerStyle.markers.values())
        crosses = [(x,y) for y in reversed(range(self.y)) for x in range(self.x)]
        data    = np.array(data)

        for (x,y),n in zip(crosses,range(len(crosses))):
            li=[self.labels.index(l) for l in labels]
            ax=plt.subplot(self.x,self.y,n+1)

            if self.sc[n] is None:
                self.sc[n] = plt.scatter(data[:,x],data[:,y],color=self.cm(li),alpha=.5)
            else:
                self.sc[n].set_offsets( np.array((data[:,x],data[:,y])).T )
                self.sc[n].set_facecolors( self.cm(li,.5) )
                self.sc[n].set_edgecolors( self.cm(li,.5) )

        return self.sc

class LinePlot:
    def __init__(self, labels, data):
        self.vspans = [] # for drawing labels
        self.arts = plt.plot(data)
        plt.tight_layout()

    def __call__(self,frameno,labels,data):
        data  = np.array(data)
        xdata = np.arange(frameno-data.shape[0], frameno)

        for art,d in zip(self.arts,data.T):
            art.set_data(xdata,d)

        #
        # remove all vspans and add updated ones
        #
        for span in self.vspans:
            span.remove()

        labels.append(None)
        offset = frameno - data.shape[0]
        spans  = [0] + [x+1 for x in range(len(labels)-1) if labels[x]!=labels[x+1]] + [len(labels)-1]
        mylabels = [l1 for l1,l2 in zip(labels,labels[1:]) if l1!=l2]
        self.vspans = [\
                plt.axvspan(offset+x1,offset+x2, alpha=.2, zorder=-1)\
                for x1,x2 in zip(spans[:-1],spans[1:]) ]
        self.vspans += [\
                plt.annotate(label, (offset+x1,0.1), xycoords=("data", "axes fraction"), rotation=30)\
                for label,x1 in zip(mylabels,spans) ]
        labels.pop()
        return self.arts


plotters = {
        'line'    : LinePlot,
        'scatter' : ScatterPlot, }

cmdline = argparse.ArgumentParser('real-time of data for grt')
cmdline.add_argument('--plot-type',   '-p', type=str, default="line",help="type of plot", choices=plotters.keys())
cmdline.add_argument('--num-samples', '-n', type=int, default=0,     help="plot the last n samples, 0 keeps all")
cmdline.add_argument('--frame-rate',  '-f', type=float, default=60., help="limit the frame-rate, 0 is unlimited")
cmdline.add_argument('--quiet',       '-q', action="store_true",     help="if given does not copy input to stdout")
cmdline.add_argument('--title',       '-t', type=str, default=None,  help="plot window title")
cmdline.add_argument('files', metavar='FILES', type=str, nargs='*', help="input files or - for stdin")
args = cmdline.parse_args()

class TextLineAnimator(Thread):
    def __init__(self, input_iterator, plotter=LinePlot, loop_func=None, framelimit=None, quiet=False):
        self.plotter = plotter
        self.frameno = 0
        self.framelimit = framelimit
        self.paused = False

        #
        # storing the input for drawing
        #
        self.labels = []
        self.data   = []

        #
        # transferring the input from stdin
        #
        self.quiet = quiet
        self.queue = Queue(framelimit)
        self.input = input_iterator
        Thread.__init__(self)
        self.start()

    def __call__(self,i):
        #
        # get the current number of elements in the
        # queue and transfer them to the buffer
        #
        qsize = self.queue.qsize()
        self.frameno += qsize

        if qsize == 0:
            return None

        while qsize > 0:
            label,data = self.queue.get()
            qsize -= 1

            self.labels.append(label)
            self.data.append(data)

            if self.framelimit and len(self.data) > self.framelimit:
                self.data.pop(0)
                self.labels.pop(0)

        labels,data = self.labels,self.data
        if isclass(self.plotter) and len(self.data) == 0:
            return []

        if isclass(self.plotter):
            self.plotter = self.plotter(labels,data)
            plt.draw()

        arts = self.plotter(self.frameno,labels,data)

        if arts is not None and len(arts) > 0:
            # rescale
            ax = arts[0].get_axes()
            ax.relim()
            ax.autoscale_view()

        return arts

    def run(self, *args):
        for line in self.input:
            while self.paused: sleep(.01)

            #
            # copy what we got to the next process
            #
            if not self.quiet: sys.stdout.write(line)

            #
            # ignore empty and comment lines
            #
            if len(line.strip()) == 0 or line.strip()[0] == '#':
                continue
            line = line.strip().split()

            #
            # add label and data item to the queue
            #
            try: self.queue.put((None, [float(x) for x in line])); continue
            except ValueError: pass
            try: self.queue.put((line[0], [float(x) for x in line[1:]]))
            except ValueError: pass

        #
        # propagate that we read input completly
        # (yes, sys.stdout.close() does not close the fd)
        # but delay this until all data has been consumed!
        #
        while self.queue.qsize() > 0:
            sleep(.1)

        os.close(sys.stdout.fileno())
        sys.stdout.close()

    def toggle_pause(self):
        self.paused = not self.paused


class MyFuncAnimation(animation.FuncAnimation):
    #
    # This is a hack to stop the animation, to avoid python busy-looping
    # when there is no more data to read. The window will still persist,
    # however as soon as stdout is closed we can be more or less sure
    # that all data has been read and drawn.
    #
    def _step(self,*args):
        animation.FuncAnimation._step(self,*args)
        return not sys.stdout.closed

if __name__=="__main__":
    fig = plt.figure()
    if args.title: fig.canvas.set_window_title(args.title)

    anim = TextLineAnimator(fileinput.input(args.files,bufsize=1),framelimit=args.num_samples,quiet=args.quiet,plotter=plotters[args.plot_type])
    afun = MyFuncAnimation(fig,anim,interval=1000./args.frame_rate)

    def press(event):
        if event.key == ' ':
            anim.toggle_pause()
            title = fig.canvas.get_window_title()
            if anim.paused: title += " [paused]"
            else: title = title.replace(" [paused]", "")
            fig.canvas.set_window_title(title)
        elif event.key == 'q': os._exit(0) # sys.exit kills only current thread!

    def resize(event):
        try: plt.tight_layout()
        except: pass

    fig.canvas.mpl_connect('key_press_event', press)
    fig.canvas.mpl_connect('resize_event', resize)
    plt.show()