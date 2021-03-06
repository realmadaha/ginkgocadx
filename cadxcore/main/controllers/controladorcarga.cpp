/*
 * This file is part of Ginkgo CADx
 *
 * Copyright (c) 2015-2016 Gert Wollny
 * Copyright (c) 2008-2014 MetaEmotion S.L. All rights reserved.
 *
 * Ginkgo CADx is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser Public License
 * along with Ginkgo CADx; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <api/globals.h>
#include <api/icommand.h>
#include <api/dicom/idicommanager.h>
#include <main/controllers/commandcontroller.h>
#include <api/internationalization/internationalization.h>
#include <api/imodelointegracion.h>
#include <main/entorno.h>
#include <commands/comandocarga.h>
#include <eventos/mensajes.h>
#include "controladorcarga.h"
#include "controladoreventos.h"
#include "controladorvistas.h"
#include "pacscontroller.h"

#include <wx/filename.h>
#include <wx/file.h>
#include <wx/thread.h>


#ifdef __DEPRECATED
#undef __DEPRECATED
#endif

#include <vtkImageData.h>
#include <vtkStringArray.h>
#include <vtkPointData.h>

#include <itkImage.h>
#include <itkCommand.h>
#include <itkMetaDataObject.h>

#include <itkImageSeriesReader.h>
#include <itkGDCMSeriesFileNames.h>
#include <itkGDCMImageIO.h>
#include <itkImageSeriesReader.h>
#include <itkVectorResampleImageFilter.h>
#include <itkImageDuplicator.h>
#include "streaming/streamingloader.h"

class CargaItkProgressCallback : public itk::Command
{
public:
        typedef CargaItkProgressCallback         Self;
        typedef itk::Command                     Superclass;

        typedef itk::SmartPointer<Self>          Pointer;
        typedef itk::SmartPointer<const Self>    ConstPointer;

        itkTypeMacro (CargaItkProgressCallback, itk::Command);
        itkNewMacro (Self);

        /** Standard Command virtual methods */
        void Execute(itk::Object *caller, const itk::EventObject &event)
        {
                itk::ProcessObject* po = dynamic_cast<itk::ProcessObject*>(caller);
                if( !po )
                        return;

                if( typeid(event) == typeid ( itk::ProgressEvent)  ) {
                        try {
                                if(!m_stop) {
                                        if (m_pComando) {
                                                if (!m_pComando->NotificarProgreso(po->GetProgress(),m_texto)) {
                                                        po->SetAbortGenerateData(true);
                                                }
                                        }
                                }
                        } catch(std::exception& /*ex*/) {
                                po->SetAbortGenerateData(true);
                                return;
                        }
                }
        }

        void Execute(const itk::Object *caller, const itk::EventObject &event)
        {
                itk::ProcessObject* po = dynamic_cast<itk::ProcessObject*>( const_cast<itk::Object*>(caller));

                if( !po ) return;

                if( typeid(event) == typeid ( itk::ProgressEvent)  ) {
                        try {
                                if(!m_stop) {
                                        if (m_pComando) {
                                                if (!m_pComando->NotificarProgreso(po->GetProgress(),m_texto)) {
                                                        po->SetAbortGenerateData(true);
                                                }
                                        }
                                }
                        } catch(std::exception& /*ex*/) {
                                po->SetAbortGenerateData(true);
                                return;
                        }
                }
        }

        void SetCommand (GNC::GCS::IComando* cmd)
        {
                m_pComando = cmd;
        }

        void SetTexto  (std::string str)
        {
                m_texto = str;
        }

protected:
        CargaItkProgressCallback()
        {
                m_pComando=NULL;
                m_stop=false;
        }

        ~CargaItkProgressCallback()
        {
                m_pComando=NULL;
        }

private:
        GNC::GCS::IComando* m_pComando;
        std::string m_texto;
        bool m_stop;
};

GNC::GCS::ControladorCarga * GNC::GCS::ControladorCarga::m_psInstancia = NULL;
wxCriticalSection* GNC::GCS::ControladorCarga::m_pCriticalSection = NULL;


vtkSmartPointer<vtkImageData> GNC::GCS::ControladorCarga::CargarITKMultidimensional(IComando* cmd, ListaRutas& listaFicheros, int* posicion, double* spacing)
{
        //primero leemos el pixeltype...
        GIL::DICOM::DicomDataset base;
        GIL::DICOM::IDICOMManager* pDicomManager = GIL::DICOM::PACSController::Instance()->CrearInstanciaDeDICOMManager();
        pDicomManager->CargarFichero(listaFicheros.front(),base);
        GIL::DICOM::PACSController::Instance()->LiberarInstanciaDeDICOMManager(pDicomManager);
        std::string tag;

        if(base.getTag(std::string("0028|0002"),tag)) {
                if(tag == "3") {
                        return CargarITKMultidimensionalRGB(cmd, listaFicheros,spacing);
                } else if (tag == "1") {
                        //return CargarVTK(listaFicheros);
                        return CargarITKMultidimensionalUnsignedShort(cmd, listaFicheros, posicion,spacing);
                } else {
                        std::ostringstream os;
                        os << _Std("Studies with") << tag << _Std(" unsupported components");
                        throw GNC::GCS::ControladorCargaException( os.str(), "ControladorCarga/CargarITKMultidimensional");
                }
        } else {
                return CargarITKMultidimensionalUnsignedShort(cmd, listaFicheros, posicion,spacing);
                //return CargarVTK(listaFicheros);
        }
}

vtkSmartPointer<vtkImageData> GNC::GCS::ControladorCarga::CargarITK(IComando* cmd, std::string& path, int* orientacion, double* spacing)
{
        ListaRutas lista;
        lista.push_back(path);
        return CargarITKMultidimensional(cmd, lista, orientacion,spacing);
}

vtkSmartPointer<vtkImageData> GNC::GCS::ControladorCarga::CargarITKMultidimensionalUnsignedShort(IComando* cmd, ListaRutas& listaFicheros, int* orientacion, double* spacing)
{
        vtkSmartPointer<vtkImageData> img = vtkSmartPointer<vtkImageData>::New();
        //int bitsStored, highBit, bitsAllocated,pixelRepresentation = 0;

        wxCriticalSectionLocker locker(*m_pCriticalSection);

        typedef double PixelType;
        typedef itk::Image<PixelType, 3 > ImageType;
        typedef itk::GDCMImageIO ImageIOType;
        typedef itk::ImageSeriesReader<ImageType> GenericReaderType;

        ImageIOType::Pointer              dicomIO    = ImageIOType::New();
        GenericReaderType::Pointer        reader     = GenericReaderType::New();
        CargaItkProgressCallback::Pointer cbProgreso = CargaItkProgressCallback::New();


        try {
                reader->SetImageIO(dicomIO);

                if (listaFicheros.size() > 0) {
                        std::vector<std::string> filesCopy(listaFicheros.size());
                        GNC::GCS::IControladorCarga::ListaRutas::iterator it = listaFicheros.begin();
                        std::vector<std::string>::size_type off = 0;
                        while (it != listaFicheros.end()) {
                                filesCopy[off++] = *(it++);
                        }
                        reader->SetFileNames(filesCopy);
                } else {
                        throw GNC::GCS::ControladorCargaException( "No input files", "ControladorCarga/CargarITKMultidimensionalUnsignedShort");
                }
                //reader->GetOutput()->ReleaseDataFlagOn();

                cbProgreso->SetCommand(cmd);
                cbProgreso->SetTexto(_Std("Reading properties"));

                reader->AddObserver (itk::ProgressEvent(), cbProgreso);

                reader->UpdateOutputInformation();
        } catch (itk::ExceptionObject& ex) {
                reader->ResetPipeline();
                throw GNC::GCS::ControladorCargaException( _Std("Error reading the study: ") + ex.GetDescription(), "ControladorCarga/CargaMultidimensional");
        } catch (std::exception& ex) {
                reader->ResetPipeline();
                throw GNC::GCS::ControladorCargaException( _Std("Internal error reading the study: "), std::string("ControladorCarga/CargaMultidimensional") + ex.what());
        } catch (...) {
                reader->ResetPipeline();
                throw GNC::GCS::ControladorCargaException( _Std("Internal error reading the study: "), "ControladorCarga/CargaMultidimensional");
        }

        if (orientacion != NULL) {

                ImageType::Pointer im = reader->GetOutput();
                ImageType::DirectionType dir = im->GetDirection();

                /*
                std::cout << dir[0][0] << ", " << dir[0][1] << ", " << dir[0][2] << std::endl;
                std::cout << dir[1][0] << ", " << dir[1][1] << ", " << dir[1][2] << std::endl;
                std::cout << dir[2][0] << ", " << dir[2][1] << ", " << dir[2][2] << std::endl;
                */

                if( std::abs( dir[0][2] ) > std::abs(dir[1][2]) && std::abs( dir[0][2]) > std::abs(dir[2][2])) {
                        //std::cout << "SAGITAL" << std::endl;
                        *orientacion = 0;
                } else if( std::abs( dir[1][2] ) > std::abs(dir[0][2]) && std::abs( dir[1][2]) > std::abs(dir[2][2])) {
                        //std::cout << "CORONAL" << std::endl;
                        *orientacion = 1;
                } else if( std::abs( dir[2][2] ) > std::abs(dir[0][2]) && std::abs( dir[2][2]) > std::abs(dir[1][2])) {
                        //std::cout << "AXIAL" << std::endl;
                        *orientacion = 2;
                }
        }

        GenericReaderType::OutputImageType::SizeType dims = reader->GetOutput()->GetLargestPossibleRegion().GetSize();

        img->SetDimensions(dims[0], dims[1], dims[2]);
        img->SetOrigin(dicomIO->GetOrigin(0), dicomIO->GetOrigin(1), dicomIO->GetOrigin(2));

        if(spacing == NULL) {
                double chk_spacing[3] = {dicomIO->GetSpacing(0), dicomIO->GetSpacing(1), dicomIO->GetSpacing(2)};
                if (chk_spacing[0] < std::numeric_limits<double>::epsilon() || chk_spacing[1] < std::numeric_limits<double>::epsilon()) {
                        std::stringstream ss;
                        ss << _Std("Spacing is not valid: (") << chk_spacing[0] << ", " << chk_spacing[1] << ", " << chk_spacing[2] << ")";
                        GNC::GCS::ControladorEventos::Instance()->ProcesarEvento(new GNC::GCS::Events::EventoMensajes(NULL,ss.str(),GNC::GCS::Events::EventoMensajes::PopUpMessage,GNC::GCS::Events::EventoMensajes::Aviso));
                        chk_spacing[0] = 1.0f;
                        chk_spacing[1] = 1.0f;
                        chk_spacing[2] = 1.0f;
                }
                img->SetSpacing(chk_spacing);
        } else {
                double chk_spacing[3] = {spacing[0], spacing[1], spacing[2]};
                if (chk_spacing[0] < std::numeric_limits<double>::epsilon() || chk_spacing[1] < std::numeric_limits<double>::epsilon()) {
                        std::stringstream ss;
                        ss << _Std("Spacing is not valid: (") << chk_spacing[0] << ", " << chk_spacing[1] << ", " << chk_spacing[2] << ")";
                        GNC::GCS::ControladorEventos::Instance()->ProcesarEvento(new GNC::GCS::Events::EventoMensajes(NULL,ss.str(),GNC::GCS::Events::EventoMensajes::PopUpMessage,GNC::GCS::Events::EventoMensajes::Aviso));
                        chk_spacing[0] = 1.0f;
                        chk_spacing[1] = 1.0f;
                        chk_spacing[2] = 1.0f;
                }
                img->SetSpacing(chk_spacing);
        }

        itk::ProcessObject::Pointer processObject;
        switch(dicomIO->GetComponentType()) {
        case ImageIOType::UCHAR: {
                typedef unsigned char TypedPixelType;
                typedef itk::Image<TypedPixelType, 3 > TypedImageType;
                typedef itk::ImageSeriesReader<TypedImageType> TypedReaderType;

                try {
                        img->AllocateScalars(VTK_UNSIGNED_CHAR, dicomIO->GetNumberOfComponents());
                } catch (...) {
                        throw GNC::GCS::ControladorCargaException( _Std("Error loading the study: Out of memory"), "ControladorCarga/CargaMultidimensional");
                }

                TypedReaderType::Pointer treader = TypedReaderType::New();
                ImageIOType::Pointer     tdicomIO = ImageIOType::New();
                treader->SetImageIO(tdicomIO);
                if (listaFicheros.size() > 0) {
                        std::vector<std::string> filesCopy(listaFicheros.size());
                        GNC::GCS::IControladorCarga::ListaRutas::iterator it = listaFicheros.begin();
                        std::vector<std::string>::size_type off = 0;

                        while (it != listaFicheros.end()) {
                                filesCopy[off++] = *(it++);
                        }
                        treader->SetFileNames(filesCopy);
                }
                treader->SetUseStreaming(true);
                //treader->GetOutput()->ReleaseDataFlagOn();
                treader->GetOutput()->GetPixelContainer()->SetImportPointer((TypedReaderType::OutputImageType::PixelType*)(img->GetScalarPointer()), dims[0] * dims[1] * dims[2], false );
                processObject = treader;
        }
        break;
        case ImageIOType::CHAR: {
                typedef char TypedPixelType;
                typedef itk::Image<TypedPixelType, 3 > TypedImageType;
                typedef itk::ImageSeriesReader<TypedImageType> TypedReaderType;

                try {
                        img->AllocateScalars(VTK_CHAR, dicomIO->GetNumberOfComponents());
                } catch (...) {
                        throw GNC::GCS::ControladorCargaException( _Std("Error loading the study: Out of memory"), "ControladorCarga/CargaMultidimensional");
                }

                TypedReaderType::Pointer treader = TypedReaderType::New();
                ImageIOType::Pointer     tdicomIO = ImageIOType::New();
                treader->SetImageIO(tdicomIO);
                if (listaFicheros.size() > 0) {
                        std::vector<std::string> filesCopy(listaFicheros.size());
                        GNC::GCS::IControladorCarga::ListaRutas::iterator it = listaFicheros.begin();
                        std::vector<std::string>::size_type off = 0;

                        while (it != listaFicheros.end()) {
                                filesCopy[off++] = *(it++);
                        }
                        treader->SetFileNames(filesCopy);
                }
                treader->SetUseStreaming(true);
                //treader->GetOutput()->ReleaseDataFlagOn();
                treader->GetOutput()->GetPixelContainer()->SetImportPointer((TypedReaderType::OutputImageType::PixelType*)(img->GetScalarPointer()), dims[0] * dims[1] * dims[2], false );
                processObject = treader;
        }
        break;
        case ImageIOType::USHORT: {
                typedef unsigned short TypedPixelType;
                typedef itk::Image<TypedPixelType, 3 > TypedImageType;
                typedef itk::ImageSeriesReader<TypedImageType> TypedReaderType;

                try {
                        img->AllocateScalars(VTK_UNSIGNED_SHORT, dicomIO->GetNumberOfComponents());
                } catch(const std::bad_alloc&) {
                        throw GNC::GCS::ControladorCargaException( _Std("Error loading the study: Out of memory"), "ControladorCarga/CargaMultidimensional");
                } catch (...) {
                        throw GNC::GCS::ControladorCargaException( _Std("Error loading the study: Internal Error"), "ControladorCarga/CargaMultidimensional");
                }

                TypedReaderType::Pointer treader = TypedReaderType::New();
                ImageIOType::Pointer     tdicomIO = ImageIOType::New();
                treader->SetImageIO(tdicomIO);
                if (listaFicheros.size() > 0) {
                        std::vector<std::string> filesCopy(listaFicheros.size());
                        GNC::GCS::IControladorCarga::ListaRutas::iterator it = listaFicheros.begin();
                        std::vector<std::string>::size_type off = 0;

                        while (it != listaFicheros.end()) {
                                filesCopy[off++] = *(it++);
                        }
                        treader->SetFileNames(filesCopy);
                }
                treader->SetUseStreaming(true);
                //treader->GetOutput()->ReleaseDataFlagOn();
                treader->GetOutput()->GetPixelContainer()->SetImportPointer((TypedReaderType::OutputImageType::PixelType*)(img->GetScalarPointer()), dims[0] * dims[1] * dims[2], false );
                processObject = treader;
        }
        break;
        case ImageIOType::SHORT: {
                typedef short TypedPixelType;
                typedef itk::Image<TypedPixelType, 3 > TypedImageType;
                typedef itk::ImageSeriesReader<TypedImageType> TypedReaderType;

                try {
                        img->AllocateScalars(VTK_SHORT, dicomIO->GetNumberOfComponents());
                } catch (...) {
                        throw GNC::GCS::ControladorCargaException( _Std("Error loading the study: Out of memory"), "ControladorCarga/CargaMultidimensional");
                }

                TypedReaderType::Pointer treader = TypedReaderType::New();
                ImageIOType::Pointer     tdicomIO = ImageIOType::New();
                treader->SetImageIO(tdicomIO);
                if (listaFicheros.size() > 0) {
                        std::vector<std::string> filesCopy(listaFicheros.size());
                        GNC::GCS::IControladorCarga::ListaRutas::iterator it = listaFicheros.begin();
                        std::vector<std::string>::size_type off = 0;

                        while (it != listaFicheros.end()) {
                                filesCopy[off++] = *(it++);
                        }
                        treader->SetFileNames(filesCopy);
                }
                treader->SetUseStreaming(true);
                //treader->GetOutput()->ReleaseDataFlagOn();
                treader->GetOutput()->GetPixelContainer()->SetImportPointer((TypedReaderType::OutputImageType::PixelType*)(img->GetScalarPointer()), dims[0] * dims[1] * dims[2], false );
                processObject = treader;
        }
        break;
        case ImageIOType::UINT: {
                typedef unsigned int TypedPixelType;
                typedef itk::Image<TypedPixelType, 3 > TypedImageType;
                typedef itk::ImageSeriesReader<TypedImageType> TypedReaderType;


                try {
                        img->AllocateScalars(VTK_UNSIGNED_INT, dicomIO->GetNumberOfComponents());
                } catch (...) {
                        throw GNC::GCS::ControladorCargaException( _Std("Error loading the study: Out of memory"), "ControladorCarga/CargaMultidimensional");
                }

                TypedReaderType::Pointer treader = TypedReaderType::New();
                ImageIOType::Pointer     tdicomIO = ImageIOType::New();
                treader->SetImageIO(tdicomIO);
                if (listaFicheros.size() > 0) {
                        std::vector<std::string> filesCopy(listaFicheros.size());
                        GNC::GCS::IControladorCarga::ListaRutas::iterator it = listaFicheros.begin();
                        std::vector<std::string>::size_type off = 0;

                        while (it != listaFicheros.end()) {
                                filesCopy[off++] = *(it++);
                        }
                        treader->SetFileNames(filesCopy);
                }
                treader->SetUseStreaming(true);
                //treader->GetOutput()->ReleaseDataFlagOn();
                treader->GetOutput()->GetPixelContainer()->SetImportPointer((TypedReaderType::OutputImageType::PixelType*)(img->GetScalarPointer()), dims[0] * dims[1] * dims[2], false );
                processObject = treader;
        }
        break;
        case ImageIOType::INT: {
                typedef int TypedPixelType;
                typedef itk::Image<TypedPixelType, 3 > TypedImageType;
                typedef itk::ImageSeriesReader<TypedImageType> TypedReaderType;


                try {
                        img->AllocateScalars(VTK_INT, dicomIO->GetNumberOfComponents());
                } catch (...) {
                        throw GNC::GCS::ControladorCargaException( _Std("Error loading the study: Out of memory"), "ControladorCarga/CargaMultidimensional");
                }

                TypedReaderType::Pointer treader = TypedReaderType::New();
                ImageIOType::Pointer     tdicomIO = ImageIOType::New();
                treader->SetImageIO(tdicomIO);
                if (listaFicheros.size() > 0) {
                        std::vector<std::string> filesCopy(listaFicheros.size());
                        GNC::GCS::IControladorCarga::ListaRutas::iterator it = listaFicheros.begin();
                        std::vector<std::string>::size_type off = 0;

                        while (it != listaFicheros.end()) {
                                filesCopy[off++] = *(it++);
                        }
                        treader->SetFileNames(filesCopy);
                }
                treader->SetUseStreaming(true);
                //treader->GetOutput()->ReleaseDataFlagOn();
                treader->GetOutput()->GetPixelContainer()->SetImportPointer((TypedReaderType::OutputImageType::PixelType*)(img->GetScalarPointer()), dims[0] * dims[1] * dims[2], false );
                processObject = treader;
        }
        break;
        case ImageIOType::ULONG: {
                typedef unsigned long TypedPixelType;
                typedef itk::Image<TypedPixelType, 3 > TypedImageType;
                typedef itk::ImageSeriesReader<TypedImageType> TypedReaderType;


                try {
                        img->AllocateScalars(VTK_UNSIGNED_LONG, dicomIO->GetNumberOfComponents());
                } catch (...) {
                        throw GNC::GCS::ControladorCargaException( _Std("Error loading the study: Out of memory"), "ControladorCarga/CargaMultidimensional");
                }

                TypedReaderType::Pointer treader = TypedReaderType::New();
                ImageIOType::Pointer     tdicomIO = ImageIOType::New();
                treader->SetImageIO(tdicomIO);
                if (listaFicheros.size() > 0) {
                        std::vector<std::string> filesCopy(listaFicheros.size());
                        GNC::GCS::IControladorCarga::ListaRutas::iterator it = listaFicheros.begin();
                        std::vector<std::string>::size_type off = 0;

                        while (it != listaFicheros.end()) {
                                filesCopy[off++] = *(it++);
                        }
                        treader->SetFileNames(filesCopy);
                }
                treader->SetUseStreaming(true);
                //treader->GetOutput()->ReleaseDataFlagOn();
                treader->GetOutput()->GetPixelContainer()->SetImportPointer((TypedReaderType::OutputImageType::PixelType*)(img->GetScalarPointer()), dims[0] * dims[1] * dims[2], false );
                processObject = treader;
        }
        break;
        case ImageIOType::LONG: {
                typedef long TypedPixelType;
                typedef itk::Image<TypedPixelType, 3 > TypedImageType;
                typedef itk::ImageSeriesReader<TypedImageType> TypedReaderType;

                try {
                        img->AllocateScalars(VTK_LONG, dicomIO->GetNumberOfComponents());
                } catch (...) {
                        throw GNC::GCS::ControladorCargaException( _Std("Error loading the study: Out of memory"), "ControladorCarga/CargaMultidimensional");
                }

                TypedReaderType::Pointer treader = TypedReaderType::New();
                ImageIOType::Pointer     tdicomIO = ImageIOType::New();
                treader->SetImageIO(tdicomIO);
                if (listaFicheros.size() > 0) {
                        std::vector<std::string> filesCopy(listaFicheros.size());
                        GNC::GCS::IControladorCarga::ListaRutas::iterator it = listaFicheros.begin();
                        std::vector<std::string>::size_type off = 0;

                        while (it != listaFicheros.end()) {
                                filesCopy[off++] = *(it++);
                        }
                        treader->SetFileNames(filesCopy);
                }
                treader->SetUseStreaming(true);
                //treader->GetOutput()->ReleaseDataFlagOn();
                treader->GetOutput()->GetPixelContainer()->SetImportPointer((TypedReaderType::OutputImageType::PixelType*)(img->GetScalarPointer()), dims[0] * dims[1] * dims[2], false );
                processObject = treader;
        }
        break;
        case ImageIOType::FLOAT: {
                typedef float TypedPixelType;
                typedef itk::Image<TypedPixelType, 3 > TypedImageType;
                typedef itk::ImageSeriesReader<TypedImageType> TypedReaderType;

                try {
                        img->AllocateScalars(VTK_FLOAT, dicomIO->GetNumberOfComponents());
                } catch (...) {
                        throw GNC::GCS::ControladorCargaException( _Std("Error loading the study: Out of memory"), "ControladorCarga/CargaMultidimensional");
                }

                TypedReaderType::Pointer treader = TypedReaderType::New();
                ImageIOType::Pointer     tdicomIO = ImageIOType::New();
                treader->SetImageIO(tdicomIO);
                if (listaFicheros.size() > 0) {
                        std::vector<std::string> filesCopy(listaFicheros.size());
                        GNC::GCS::IControladorCarga::ListaRutas::iterator it = listaFicheros.begin();
                        std::vector<std::string>::size_type off = 0;

                        while (it != listaFicheros.end()) {
                                filesCopy[off++] = *(it++);
                        }
                        treader->SetFileNames(filesCopy);
                }
                treader->SetUseStreaming(true);
                //treader->GetOutput()->ReleaseDataFlagOn();
                treader->GetOutput()->GetPixelContainer()->SetImportPointer((TypedReaderType::OutputImageType::PixelType*)(img->GetScalarPointer()), dims[0] * dims[1] * dims[2], false );
                processObject = treader;
        }
        break;
        case ImageIOType::DOUBLE: {
                typedef double TypedPixelType;
                typedef itk::Image<TypedPixelType, 3 > TypedImageType;
                typedef itk::ImageSeriesReader<TypedImageType> TypedReaderType;

                try {
                        img->AllocateScalars(VTK_DOUBLE, dicomIO->GetNumberOfComponents());
                } catch (...) {
                        throw GNC::GCS::ControladorCargaException( _Std("Error loading the study: Out of memory"), "ControladorCarga/CargaMultidimensional");
                }

                TypedReaderType::Pointer treader = TypedReaderType::New();
                ImageIOType::Pointer     tdicomIO = ImageIOType::New();
                treader->SetImageIO(tdicomIO);
                if (listaFicheros.size() > 0) {
                        std::vector<std::string> filesCopy(listaFicheros.size());
                        GNC::GCS::IControladorCarga::ListaRutas::iterator it = listaFicheros.begin();
                        std::vector<std::string>::size_type off = 0;

                        while (it != listaFicheros.end()) {
                                filesCopy[off++] = *(it++);
                        }
                        treader->SetFileNames(filesCopy);
                }
                treader->SetUseStreaming(true);
                //treader->GetOutput()->ReleaseDataFlagOn();
                treader->GetOutput()->GetPixelContainer()->SetImportPointer((TypedReaderType::OutputImageType::PixelType*)(img->GetScalarPointer()), dims[0] * dims[1] * dims[2], false );
                processObject = treader;
        }
        break;
        case ImageIOType::UNKNOWNCOMPONENTTYPE:
        default:
                throw GNC::GCS::ControladorCargaException( _Std("Error reading the study: unsupported pixel format"), "ControladorCarga/CargaMultidimensional");
        }

        cbProgreso->SetTexto("Leyendo dataset");
        processObject->AddObserver (itk::ProgressEvent(), cbProgreso);

        try {
                processObject->UpdateLargestPossibleRegion();
        } catch (itk::ExceptionObject& ex) {
                reader->ResetPipeline();
                throw GNC::GCS::ControladorCargaException( _Std("Error reading the study: ") + ex.GetDescription(), "ControladorCarga/CargaMultidimensional");
        } catch (...) {
                reader->ResetPipeline();
                throw GNC::GCS::ControladorCargaException( _Std("Internal error reading the study: "), "ControladorCarga/CargaMultidimensional");
        }
        if (processObject->GetAbortGenerateData()) {
                reader->ResetPipeline();
                throw GNC::GCS::ControladorCargaException( _Std("Process canceled by user."), "ControladorCarga/CargaMultidimensional");
        }


        //se modifica si es necesario
        /*
        {
        	typedef itk::MetaDataObject< std::string > MetaDataStringType;
        	for (itk::MetaDataDictionary::ConstIterator it = dicomIO->GetMetaDataDictionary().Begin(); it != dicomIO->GetMetaDataDictionary().End(); ++it) {
        		itk::MetaDataObjectBase::Pointer entry = it->second;
        		MetaDataStringType::Pointer entryvalue = dynamic_cast<MetaDataStringType *> (entry.GetPointer());
        		if(it->first == "0028|0100") { //bits allocated
        			std::istringstream is(entryvalue->GetMetaDataObjectValue());
        			is >>bitsAllocated;
        		} else if(it->first == "0028|0101") { //bits stored
        			std::istringstream is(entryvalue->GetMetaDataObjectValue());
        			is >>bitsStored;
        		} else if(it->first == "0028|0102") {//high bit
        			std::istringstream is(entryvalue->GetMetaDataObjectValue());
        			is >>highBit;
        		}	else if(it->first == "0028|0103") {//pixel representation=> 0 es unsigned 1 es signed
        			std::istringstream is(entryvalue->GetMetaDataObjectValue());
        			is >>pixelRepresentation;
        		}
        	}
        	if(bitsAllocated != bitsStored)
        	{
        		switch(dicomIO->GetComponentType()) {
        			case ImageIOType::UCHAR:
        				pixelRepresentation = 0;
        				//a partir de aqui se trata el pixelrepresentation
        			case ImageIOType::CHAR:
        				{
        					unsigned char* data = (unsigned char*) img->GetScalarPointer();
        					unsigned char desplazamientoSigno;
        					desplazamientoSigno = highBit;
        					unsigned char maskComprobarSigno = 1;
        					maskComprobarSigno <<= desplazamientoSigno;

        					unsigned char maskClearParteAltaPositivo = 0;
        					//se meten unos en la parte baja
        					if(pixelRepresentation == 0) {
        						for(int i = 0; i<= desplazamientoSigno; ++i)
        						{
        							maskClearParteAltaPositivo <<=1;
        							maskClearParteAltaPositivo |=1;
        						}
        					} else {
        						for(int i = 0; i< desplazamientoSigno; ++i)
        						{
        							maskClearParteAltaPositivo <<=1;
        							maskClearParteAltaPositivo |=1;
        						}
        					}
        					//se meten unos en la parte alta
        					unsigned char maskSetParteAltaNegativo = 0x80;
        					if(pixelRepresentation != 0) {
        						for(int i=0; i< 8-desplazamientoSigno; ++i)
        						{
        							maskSetParteAltaNegativo >>=1;
        							maskSetParteAltaNegativo |=0x80;
        						}
        					}

        					int size = dims[0] * dims[1] * dims[2];
        					if(pixelRepresentation == 0) {
        						if(maskClearParteAltaPositivo != 0xFF) { // si es ff no tiene sentido hacer nada
        							int size = dims[0] * dims[1] * dims[2] * 2;
        							for(int i= 0; i< size; i+=2)
        							{
        								//es positivo
        								data[i] &= maskClearParteAltaPositivo;
        							}
        						}
        					} else {
        						for(int i= 0; i< size; ++i)
        						{
        							if((data[i] & maskComprobarSigno) == 0)
        							{
        								//es positivo
        								data[i] &= maskClearParteAltaPositivo;
        							} else {
        								//es negativo => aplicar el complemento a dos...
        								data[i] |= maskSetParteAltaNegativo;
        							}
        						}
        					}
        				}
        				break;
        			case ImageIOType::USHORT:
        				pixelRepresentation = 0;
        				//a partir de aqui se trata el pixelrepresentation
        			case ImageIOType::SHORT:
        				{
        					unsigned char* data = (unsigned char*) img->GetScalarPointer();
        					unsigned char posicionInicial;
        					unsigned char desplazamientoSigno;
        					if(highBit>=8) { //little endian
        						desplazamientoSigno = highBit - 8 ;
        						posicionInicial = 1;
        					} else { //bigEndian
        						desplazamientoSigno = highBit;
        						posicionInicial = 0;
        					}
        					unsigned char maskComprobarSigno = 1;
        					maskComprobarSigno <<= desplazamientoSigno;

        					unsigned char maskClearParteAltaPositivo = 0;
        					//se meten unos en la parte baja
        					if(pixelRepresentation == 0) {
        						for(int i = 0; i<= desplazamientoSigno; ++i)
        						{
        							maskClearParteAltaPositivo <<=1;
        							maskClearParteAltaPositivo |=1;
        						}
        					} else {
        						for(int i = 0; i< desplazamientoSigno; ++i)
        						{
        							maskClearParteAltaPositivo <<=1;
        							maskClearParteAltaPositivo |=1;
        						}
        					}
        					//se meten unos en la parte alta
        					unsigned char maskSetParteAltaNegativo = 0x80;
        					if(pixelRepresentation != 0) {
        						for(int i=0; i< 8-desplazamientoSigno; ++i)
        						{
        							maskSetParteAltaNegativo >>=1;
        							maskSetParteAltaNegativo |=0x80;
        						}
        					}

        					int size = dims[0] * dims[1] * dims[2] * 2;

        					if(pixelRepresentation == 0) {
        						if(maskClearParteAltaPositivo != 0xFF) { // si es ff no tiene sentido hacer nada
        							int size = dims[0] * dims[1] * dims[2] * 2;
        							for(int i= posicionInicial; i< size; i+=2)
        							{
        								//es positivo
        								data[i] &= maskClearParteAltaPositivo;
        							}
        						}
        					} else {
        						for(int i= posicionInicial; i< size; i+=2)
        						{
        							if((data[i] & maskComprobarSigno) == 0)
        							{
        								//es positivo
        								data[i] &= maskClearParteAltaPositivo;
        							} else {
        								//es negativo => aplicar el complemento a dos...
        								data[i] |= maskSetParteAltaNegativo;
        							}
        						}
        					}
        				}
        				break;
        			case ImageIOType::UINT:
        				{
        					//d momento no hago na
        				}
        				break;
        			case ImageIOType::INT:
        				{
        					//d momento no hago na
        				}
        				break;
        			case ImageIOType::ULONG:
        				{
        					//d momento no hago na
        				}
        				break;
        			case ImageIOType::LONG:
        				{
        					//d momento no hago na
        				}
        				break;
        			case ImageIOType::FLOAT:
        			case ImageIOType::DOUBLE:
        				break;
        			case ImageIOType::UNKNOWNCOMPONENTTYPE:
        			default:
        				throw GNC::GCS::ControladorCargaException( std::string("Error reading the study: Formato de pixel no soportado"), "ControladorCarga/CargaMultidimensional");
        		}
        	}
        }*/



        return img;
}

vtkSmartPointer<vtkImageData> GNC::GCS::ControladorCarga::CargarITKMultidimensionalRGB(IComando* cmd, ListaRutas& listaFicheros, double* spacing)
{

        vtkSmartPointer<vtkImageData> img = vtkSmartPointer<vtkImageData>::New();

        wxCriticalSectionLocker locker(*m_pCriticalSection);

        typedef itk::RGBPixel<unsigned char> PixelType;
        typedef itk::Image<PixelType, 3 > ImageType;
        typedef itk::GDCMImageIO ImageIOType;
        typedef itk::ImageSeriesReader<ImageType> GenericReaderType;

        ImageIOType::Pointer              dicomIO    = ImageIOType::New();
        GenericReaderType::Pointer        reader     = GenericReaderType::New();
        CargaItkProgressCallback::Pointer cbProgreso = CargaItkProgressCallback::New();

        reader->SetImageIO(dicomIO);

        if (listaFicheros.size() > 0) {
                std::vector<std::string> filesCopy(listaFicheros.size());
                GNC::GCS::IControladorCarga::ListaRutas::iterator it = listaFicheros.begin();
                std::vector<std::string>::size_type off = 0;

                while (it != listaFicheros.end()) {
                        filesCopy[off++] = *(it++);
                }
                reader->SetFileNames(filesCopy);
        } else {
                throw GNC::GCS::ControladorCargaException( "No input files", "ControladorCarga/CargaMultidimensionalRGB");
        }
        reader->SetUseStreaming(true);
        //reader->GetOutput()->ReleaseDataFlagOn();

        cbProgreso->SetCommand(cmd);
        cbProgreso->SetTexto(_Std("Interpreting properties"));

        reader->AddObserver (itk::ProgressEvent(), cbProgreso);

        try {
                reader->UpdateOutputInformation();
        } catch (itk::ExceptionObject& ex) {
                reader->ResetPipeline();
                throw GNC::GCS::ControladorCargaException( _Std("Error reading the study: ") + ex.GetDescription(), "ControladorCarga/CargaMultidimensionalRGB");
        } catch (...) {
                reader->ResetPipeline();
                throw GNC::GCS::ControladorCargaException( _Std("Internal error reading the study: "), "ControladorCarga/CargaMultidimensionalRGB");
        }

        GenericReaderType::OutputImageType::SizeType dims = reader->GetOutput()->GetLargestPossibleRegion().GetSize();

        img->SetDimensions(dims[0], dims[1], dims[2]);
        img->SetOrigin(dicomIO->GetOrigin(0), dicomIO->GetOrigin(1), dicomIO->GetOrigin(2));

        if(spacing == NULL) {
                double chk_spacing[3] = {dicomIO->GetSpacing(0), dicomIO->GetSpacing(1), dicomIO->GetSpacing(2)};
                if (chk_spacing[0] < std::numeric_limits<double>::epsilon() || chk_spacing[1] < std::numeric_limits<double>::epsilon()) {
                        std::stringstream ss;
                        ss << _Std("Spacing is not valid: (") << chk_spacing[0] << ", " << chk_spacing[1] << ", " << chk_spacing[2] << ")";
                        GNC::GCS::ControladorEventos::Instance()->ProcesarEvento(new GNC::GCS::Events::EventoMensajes(NULL,ss.str(),GNC::GCS::Events::EventoMensajes::PopUpMessage,GNC::GCS::Events::EventoMensajes::Aviso));
                        chk_spacing[0] = 1.0f;
                        chk_spacing[1] = 1.0f;
                        chk_spacing[2] = 1.0f;
                }
                img->SetSpacing(chk_spacing);
        } else {
                double chk_spacing[3] = {spacing[0], spacing[1], spacing[2]};
                if (chk_spacing[0] < std::numeric_limits<double>::epsilon() || chk_spacing[1] < std::numeric_limits<double>::epsilon()) {
                        std::stringstream ss;
                        ss << _Std("Spacing of the image is invalid: (") << chk_spacing[0] << ", " << chk_spacing[1] << ", " << chk_spacing[2] << ")";
                        GNC::GCS::ControladorEventos::Instance()->ProcesarEvento(new GNC::GCS::Events::EventoMensajes(NULL,ss.str(),GNC::GCS::Events::EventoMensajes::PopUpMessage,GNC::GCS::Events::EventoMensajes::Aviso));
                        chk_spacing[0] = 1.0f;
                        chk_spacing[1] = 1.0f;
                        chk_spacing[2] = 1.0f;
                }
                img->SetSpacing(chk_spacing);
        }

        //std::cout << "number of scalar components" << dicomIO->GetNumberOfComponents();

        try {
                img->AllocateScalars(VTK_UNSIGNED_CHAR, dicomIO->GetNumberOfComponents());
        } catch (...) {
                throw GNC::GCS::ControladorCargaException( _Std("Error loading the study: Out of memory"), "ControladorCarga/CargaMultidimensionalRGB");
        }

        reader->SetUseStreaming(true);
        //reader->GetOutput()->ReleaseDataFlagOn();
        reader->GetOutput()->GetPixelContainer()->SetImportPointer((GenericReaderType::OutputImageType::PixelType*)(img->GetScalarPointer()), dims[0] * dims[1] * dims[2], false );

        cbProgreso->SetTexto(_Std("Reading dataset"));
        reader->AddObserver (itk::ProgressEvent(), cbProgreso);

        try {
                reader->UpdateLargestPossibleRegion();
        } catch (itk::ExceptionObject& ex) {
                reader->ResetPipeline();
                throw GNC::GCS::ControladorCargaException( _Std("Error reading the study:") + ex.GetDescription(), "ControladorCarga/CargaMultidimensionalRGB");
        } catch (...) {
                reader->ResetPipeline();
                throw GNC::GCS::ControladorCargaException( _Std("Internal error while reading the study:"), "ControladorCarga/CargaMultidimensionalRGB");
        }
        if (reader->GetAbortGenerateData()) {
                reader->ResetPipeline();
                throw GNC::GCS::ControladorCargaException( _Std("Process canceled by user."), "ControladorCarga/CargaMultidimensional");
        }

        return img;

}

void GNC::GCS::ControladorCarga::CargaAsincrona(GNC::GCS::IVista* pVista, bool loadFirst)
{
        wxCriticalSectionLocker locker(*m_pCriticalSection);
        GNC::GCS::ControladorVistas::Instance()->SolicitarActivarVista(pVista);
        GADAPI::ComandoCarga* pCmd = new GADAPI::ComandoCarga(new GADAPI::ComandoCargaParams(pVista, loadFirst));
        GNC::Entorno::Instance()->GetCommandController()->ProcessAsync(_Std("load"), pCmd, pVista);
}

GNC::GCS::ControladorCarga * GNC::GCS::ControladorCarga::Instance()
{
        if (m_pCriticalSection == NULL) {
                m_pCriticalSection = new wxCriticalSection();
        }

        wxCriticalSectionLocker locker(*m_pCriticalSection);

        if (m_psInstancia == NULL) {
                m_psInstancia = new ControladorCarga();
        }
        return m_psInstancia;
}

void GNC::GCS::ControladorCarga::FreeInstance()
{
        wxCriticalSectionLocker* pLocker = NULL;
        if (m_pCriticalSection != NULL) {
                pLocker = new wxCriticalSectionLocker(*m_pCriticalSection);
        }
        if (m_psInstancia != NULL) {
                delete m_psInstancia;
                m_psInstancia = NULL;
        }
        if (m_pCriticalSection != NULL) {
                if (pLocker != NULL) {
                        delete pLocker;
                        pLocker = NULL;
                }
                delete m_pCriticalSection;
                m_pCriticalSection = NULL;
        }
        if (pLocker != NULL) {
                delete pLocker;
        }
}

GNC::GCS::ControladorCarga::ControladorCarga()
{
}

GNC::GCS::ControladorCarga::~ControladorCarga()
{
}

//region Creacion y destruccion de componente de carga en streaming
GNC::GCS::IStreamingLoader* GNC::GCS::ControladorCarga::NewLoader()
{
        return new GNC::StreamingLoader();
}

void GNC::GCS::ControladorCarga::FreeLoader(GNC::GCS::IStreamingLoader** loader)
{
        if (loader != NULL && *loader != NULL) {
                delete *loader;
                *loader = NULL;
        }

}
//endregion

