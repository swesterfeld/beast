#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html

# coding=utf-8

import sys
import subprocess

from PyQt5 import QtGui, QtCore, QtWidgets

class BlepWidget(QtWidgets.QWidget):
  def __init__(self):
    super(BlepWidget, self).__init__()
    self.setMinimumSize (500, 500)

  def update_params (self, params):
    try:
      process = subprocess.Popen(['testblep', 'plotblep',
        "%.5f" % params["shape"].value,
        "%.5f" % params["sync"].value,
        "%.5f" % params["sub"].value,
        "%.5f" % params["pulse"].value], stdout=subprocess.PIPE)
      out, err = process.communicate()
      self.plot_data = []
      for o in out.splitlines():
        self.plot_data += [ float (o) ]
      self.repaint()
    except KeyError:
      # hacky way of avoiding to run testblep if not all params are known
      pass

  def paintEvent(self, event):
    qp = QtGui.QPainter()
    qp.begin(self)
    xscale = 0.25
    yscale = -self.height() / 2 * 0.75
    ycenter = self.height() / 2
    for i in range (len (self.plot_data) - 1):
      qp.drawLine (i * xscale, self.plot_data[i] * yscale + ycenter, (i + 1) * xscale, self.plot_data[i+1] * yscale + ycenter)

    qp.setPen (QtGui.QColor(168, 34, 3))
    qp.drawLine (0, ycenter, self.width(), ycenter)

    qp.end()

class Param:
  pass

class PlotWindow (QtWidgets.QMainWindow):
  def __init__ (self):
    QtWidgets.QMainWindow.__init__(self)

    self.init_ui()

  def init_ui (self):
    central_widget = QtWidgets.QWidget (self)
    self.grid_layout = QtWidgets.QGridLayout()

    self.blep_widget = BlepWidget ()
    self.grid_layout.addWidget (self.blep_widget, 1, 0, 3, 1)
    central_widget.setLayout (self.grid_layout)
    self.setCentralWidget (central_widget)

    self.params = dict()
    self.add_slider (1, "shape", 0, -100, 100)
    self.add_slider (2, "sync", 0, 0, 60)
    self.add_slider (3, "sub", 0, 0, 100)
    self.add_slider (4, "pulse", 50, 0, 100)

  def add_slider (self, row, label, vdef, vmin, vmax):
    slider = QtWidgets.QSlider (QtCore.Qt.Vertical)
    slider.setRange (vmin * 10, vmax * 10)
    slider.setValue (vdef * 10)
    slider.setEnabled (True)
    slider.valueChanged.connect (lambda value: self.on_param_changed (label, value))
    self.params[label] = Param()
    self.params[label].value_label = QtWidgets.QLabel()
    self.on_param_changed (label, vdef * 10)
    self.grid_layout.addWidget (slider, 3, row, 1, 1, QtCore.Qt.AlignHCenter)

    self.grid_layout.addWidget (QtWidgets.QLabel (label), 1, row, 1, 1, QtCore.Qt.AlignHCenter)
    self.grid_layout.addWidget (self.params[label].value_label, 2, row, 1, 1, QtCore.Qt.AlignHCenter)
    self.grid_layout.setColumnStretch (0, 1)

  def on_param_changed (self, label, value):
    self.params[label].value = value / 10
    self.params[label].value_label.setText ("%s" % self.params[label].value)
    self.blep_widget.update_params (self.params)

def main():
  app = QtWidgets.QApplication (sys.argv)
  plot = PlotWindow()
  plot.show()
  sys.exit (app.exec_())

if __name__ == "__main__":
  main()
