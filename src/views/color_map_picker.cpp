#include "color_map_picker.hpp"
#include "color_bar.hpp"
#include <QtWidgets>
#include <cmath>

ColorMapPicker::ColorMapPicker(const std::vector<HdfColorMap::Palette> &registry,int selected,
                             const HdfColorMap::Mapping &settings,QWidget *parent)
  :QDialog(parent),palettes(registry),initial(settings),under(settings.under),over(settings.over),result_mapping(settings) {
  setObjectName("colorMapDialog");setWindowTitle(tr("CMaps"));resize(620,570);
  auto *layout=new QVBoxLayout(this);
  auto *search=new QLineEdit(this);search->setPlaceholderText(tr("Search color cards"));layout->addWidget(search);
  selector=new QComboBox(this);selector->setObjectName("colorCardSelector");selector->setIconSize(QSize(128,18));
  for(size_t index=0;index<palettes.size();++index)
    selector->addItem(color_map_icon(palettes[index]),palettes[index].group+" / "+palettes[index].name,int(index));
  selector->setCurrentIndex(selected);layout->addWidget(selector);
  auto *details=new QPlainTextEdit(this);details->setReadOnly(true);details->setMaximumHeight(145);layout->addWidget(details);
  auto update_details=[this,details] {
    const auto &item=palettes[size_t(selector->currentData().toInt())];
    details->setPlainText(QString("%1 | %2 colors | %3\n%4\n%5\nLicense: %6\nRevision: %7\nSHA256: %8\n%9")
      .arg(item.id).arg(item.colors.size()).arg(item.kind,item.authors.join(", "),item.source,item.license,item.revision,item.source_hash,item.modification));
  };
  connect(selector,&QComboBox::currentIndexChanged,this,update_details);update_details();
  connect(search,&QLineEdit::textChanged,this,[this](const QString &text){
    auto *view=qobject_cast<QListView*>(selector->view());if(!view) return;
    for(int index=0;index<selector->count();++index) view->setRowHidden(index,!selector->itemText(index).contains(text,Qt::CaseInsensitive));
  });
  auto *form=new QFormLayout;layout->addLayout(form);
  mode=new QComboBox(this);mode->setObjectName("colorMappingMode");mode->addItems({tr("Continuous"),tr("Stepped")});mode->setCurrentIndex(int(settings.mode));form->addRow(tr("Rendering"),mode);
  normalization=new QComboBox(this);normalization->setObjectName("colorNormalization");normalization->addItems({tr("Linear"),tr("Centered"),tr("Cyclic")});normalization->setCurrentIndex(int(settings.normalization));form->addRow(tr("Normalization"),normalization);
  reverse=new QCheckBox(tr("Reverse"),this);reverse->setChecked(settings.reversed);form->addRow(reverse);
  center=new QLineEdit(QString::number(settings.center,'g',15),this);center->setObjectName("colorCenter");form->addRow(tr("Center (decoded units)"),center);
  origin=new QLineEdit(QString::number(settings.origin,'g',15),this);origin->setObjectName("colorOrigin");form->addRow(tr("Cycle origin"),origin);
  period=new QLineEdit(QString::number(settings.period,'g',15),this);period->setObjectName("colorPeriod");form->addRow(tr("Cycle period (same units as data)"),period);
  QStringList values;for(double value:settings.boundaries) values<<QString::number(value,'g',15);
  boundaries=new QLineEdit(values.join(", "),this);boundaries->setObjectName("colorBoundaries");boundaries->setPlaceholderText(tr("Empty = equal display intervals; K colors need K+1 boundaries"));form->addRow(tr("Boundaries"),boundaries);
  auto update_enabled=[this] {
    center->setEnabled(normalization->currentIndex()==1);
    origin->setEnabled(normalization->currentIndex()==2);period->setEnabled(normalization->currentIndex()==2);
    boundaries->setEnabled(normalization->currentIndex()==0&&mode->currentIndex()==1);
  };
  connect(normalization,&QComboBox::currentIndexChanged,this,update_enabled);connect(mode,&QComboBox::currentIndexChanged,this,update_enabled);update_enabled();
  connect(selector,&QComboBox::currentIndexChanged,this,[this] {
    const auto &palette=palettes[size_t(selected_index())];
    mode->setCurrentIndex(palette.stepped?1:0);
    boundaries->clear();
  });
  auto *row=new QHBoxLayout;
  custom_under=new QCheckBox(tr("Under color"),this);custom_under->setChecked(settings.custom_under);
  custom_over=new QCheckBox(tr("Over color"),this);custom_over->setChecked(settings.custom_over);
  auto *under_button=new QPushButton(tr("Choose…"),this);auto *over_button=new QPushButton(tr("Choose…"),this);
  row->addWidget(custom_under);row->addWidget(under_button);row->addWidget(custom_over);row->addWidget(over_button);form->addRow(row);
  connect(under_button,&QPushButton::clicked,this,[this]{auto color=QColorDialog::getColor(QColor::fromRgba(under),this);if(color.isValid()){under=color.rgba();custom_under->setChecked(true);}});
  connect(over_button,&QPushButton::clicked,this,[this]{auto color=QColorDialog::getColor(QColor::fromRgba(over),this);if(color.isValid()){over=color.rgba();custom_over->setChecked(true);}});
  auto *hint=new QLabel(tr("Colors do not define units, accumulation periods or meteorological thresholds.\nCyclic mapping requires a confirmed period (e.g. 360 degrees or 2π radians)."),this);hint->setWordWrap(true);layout->addWidget(hint);
  auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);layout->addWidget(buttons);
  connect(buttons,&QDialogButtonBox::accepted,this,&ColorMapPicker::apply);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
}
int ColorMapPicker::selected_index() const {return selector->currentData().toInt();}
HdfColorMap::Mapping ColorMapPicker::selected_mapping() const {return result_mapping;}
void ColorMapPicker::apply() {
  auto settings=initial;settings.mode=HdfColorMap::Mode(mode->currentIndex());settings.normalization=HdfColorMap::Normalization(normalization->currentIndex());
  settings.reversed=reverse->isChecked();settings.boundaries.clear();
  auto number=[](QLineEdit *edit,double &value){bool ok=false;value=edit->text().toDouble(&ok);return ok&&std::isfinite(value);};
  QString error;
  if(center->isEnabled()&&!number(center,settings.center)) error=tr("Center must be a finite number.");
  if(origin->isEnabled()&&(!number(origin,settings.origin)||!number(period,settings.period))) error=tr("Origin and period must be finite numbers.");
  if(boundaries->isEnabled()&&!boundaries->text().trimmed().isEmpty()) {
    for(const QString &text:boundaries->text().split(QRegularExpression("[,;\\s]+"),Qt::SkipEmptyParts)) {
      bool ok=false;double value=text.toDouble(&ok);if(!ok||!std::isfinite(value)){error=tr("Invalid boundary.");break;}settings.boundaries.push_back(value);
    }
    if(!settings.boundaries.empty()){settings.minimum=settings.boundaries.front();settings.maximum=settings.boundaries.back();}
  }
  settings.custom_under=custom_under->isChecked();settings.custom_over=custom_over->isChecked();settings.under=under;settings.over=over;
  if(error.isEmpty()) error=HdfColorMap::validate(palettes[size_t(selected_index())],settings);
  if(!error.isEmpty()){QMessageBox::warning(this,tr("CMaps"),error);return;}
  result_mapping=settings;accept();
}
